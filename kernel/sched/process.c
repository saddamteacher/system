/*
 * State:
 *   process_lock protects the process list, the pid counter, and the
 *   `exited` / `exit_code` fields of every process.
 *   A process's file table and address space pointer are used only by its
 *   own thread (and by its creator before that thread starts).
 * Lock order: process_lock, then the scheduler lock (via the wait queue).
 */
#include <sched/process.h>

#include <saddev/errno.h>
#include <saddev/syscall.h>

#include <arch/trap.h>
#include <arch/user.h>
#include <core/list.h>
#include <core/log.h>
#include <core/panic.h>
#include <core/spinlock.h>
#include <core/string.h>
#include <fs/vfs.h>
#include <mm/aspace.h>
#include <mm/heap.h>
#include <sched/elf.h>
#include <sched/thread.h>
#include <sched/wait_queue.h>

#define USER_STACK_PAGES 64
#define PROCESS_NAME_LENGTH 32
#define STANDARD_FILE_COUNT 3 /* input, output, error */

struct process {
    int pid;
    char name[PROCESS_NAME_LENGTH];
    struct address_space *space;
    struct file *files[PROCESS_MAX_FILES];

    /* Where the program starts, for the thread's first entry to user mode. */
    uintptr_t entry;
    uintptr_t stack_pointer;
    int argc;
    uintptr_t argv;

    struct list_node link;
    bool detached; /* free the record at exit; nobody will wait */
    bool exited;
    int exit_code;
    struct wait_queue exit_waiters;
};

static struct spinlock process_lock = SPINLOCK_INIT;
static struct list_node processes = { &processes, &processes };
static int next_pid = 1;
static size_t live_processes;

struct process *process_current(void)
{
    return thread_owner(thread_current());
}

int process_id(const struct process *process)
{
    return process->pid;
}

struct address_space *process_address_space(struct process *process)
{
    return process->space;
}

size_t process_count(void)
{
    arch_irq_state_t irq_state = spinlock_acquire(&process_lock);
    size_t count = live_processes;
    spinlock_release(&process_lock, irq_state);
    return count;
}

/* ------------------------------------------------------------ files */

int process_file_install(struct process *process, struct file *file)
{
    for (int descriptor = 0; descriptor < PROCESS_MAX_FILES; descriptor++) {
        if (process->files[descriptor] == NULL) {
            process->files[descriptor] = file;
            return descriptor;
        }
    }
    return -EMFILE;
}

struct file *process_file_get(struct process *process, int descriptor)
{
    if (descriptor < 0 || descriptor >= PROCESS_MAX_FILES) {
        return NULL;
    }
    return process->files[descriptor];
}

int process_file_remove(struct process *process, int descriptor)
{
    struct file *file = process_file_get(process, descriptor);
    if (file == NULL) {
        return -EBADF;
    }
    process->files[descriptor] = NULL;
    vfs_close(file);
    return 0;
}

static void close_all_files(struct process *process)
{
    for (int descriptor = 0; descriptor < PROCESS_MAX_FILES; descriptor++) {
        if (process->files[descriptor] != NULL) {
            vfs_close(process->files[descriptor]);
            process->files[descriptor] = NULL;
        }
    }
}

static int setup_standard_files(struct process *process, struct process *parent)
{
    for (int descriptor = 0; descriptor < STANDARD_FILE_COUNT; descriptor++) {
        struct file *file = parent != NULL ? parent->files[descriptor] : NULL;
        if (file != NULL) {
            vfs_retain(file);
        } else {
            int status = vfs_open("/dev/console", &file);
            if (status < 0) {
                return status;
            }
        }
        process->files[descriptor] = file;
    }
    return 0;
}

/* ------------------------------------------------------- program image */

/* Reads a whole file into a kernel buffer the caller must kfree(). */
static int read_whole_file(const char *path, void **data, size_t *size)
{
    struct file *file;
    int status = vfs_open(path, &file);
    if (status < 0) {
        return status;
    }
    void *buffer = kmalloc(file->size > 0 ? file->size : 1);
    if (buffer == NULL) {
        vfs_close(file);
        return -ENOMEM;
    }
    int64_t count = vfs_read(file, buffer, file->size);
    *size = file->size;
    vfs_close(file);

    if (count < 0 || (uint64_t)count != *size) {
        kfree(buffer);
        return count < 0 ? (int)count : -EIO;
    }
    *data = buffer;
    return 0;
}

/*
 * Creates the user stack and puts the arguments on it:
 *
 *   high addresses   "arg0\0" "arg1\0" ...      the strings
 *                    argv[0] argv[1] ... NULL   pointers to them
 *   stack pointer -> (16-byte aligned)
 */
static int setup_stack(struct process *process, int argc, const char *const argv[])
{
    uintptr_t stack_top = arch_mmu_user_end();
    uintptr_t stack_bottom = stack_top - USER_STACK_PAGES * ARCH_PAGE_SIZE;
    /* The page below stays unmapped, so a stack overflow faults. */
    if (!aspace_map(process->space, stack_bottom, USER_STACK_PAGES, ARCH_MAP_WRITE)) {
        return -ENOMEM;
    }

    uintptr_t pointers[SD_ARG_MAX + 1];
    uintptr_t cursor = stack_top;
    for (int i = 0; i < argc; i++) {
        size_t length = strlen(argv[i]) + 1;
        cursor -= length;
        if (!aspace_write(process->space, cursor, argv[i], length)) {
            return -EFAULT;
        }
        pointers[i] = cursor;
    }
    pointers[argc] = 0;

    size_t table_size = (size_t)(argc + 1) * sizeof(uintptr_t);
    cursor = (cursor - table_size) & ~(uintptr_t)15;
    if (!aspace_write(process->space, cursor, pointers, table_size)) {
        return -EFAULT;
    }
    process->argc = argc;
    process->argv = cursor;
    process->stack_pointer = cursor;
    return 0;
}

static int load_program(struct process *process, const char *path, int argc, const char *const argv[])
{
    void *image;
    size_t size;
    int status = read_whole_file(path, &image, &size);
    if (status < 0) {
        return status;
    }
    status = elf_load(process->space, image, size, &process->entry);
    kfree(image);
    if (status < 0) {
        return status;
    }
    return setup_stack(process, argc, argv);
}

/* ------------------------------------------------------------ lifecycle */

/* The process's thread starts here, in kernel mode, and leaves for good. */
static void process_thread(void *argument)
{
    struct process *process = argument;
    arch_enter_user(process->entry, process->stack_pointer, (uint64_t)process->argc, process->argv);
}

static void set_name(struct process *process, const char *path)
{
    /* The last path component. */
    const char *name = path;
    for (const char *cursor = path; *cursor != '\0'; cursor++) {
        if (*cursor == '/') {
            name = cursor + 1;
        }
    }
    size_t length = strnlen(name, PROCESS_NAME_LENGTH - 1);
    memcpy(process->name, name, length);
    process->name[length] = '\0';
}

static void destroy_unstarted(struct process *process)
{
    close_all_files(process);
    if (process->space != NULL) {
        aspace_destroy(process->space);
    }
    kfree(process);
}

int process_spawn(const char *path, int argc, const char *const argv[], struct process *parent)
{
    if (argc < 0 || argc > SD_ARG_MAX) {
        return -EINVAL;
    }
    struct process *process = kzalloc(sizeof(*process));
    if (process == NULL) {
        return -ENOMEM;
    }
    set_name(process, path);
    wait_queue_init(&process->exit_waiters);
    process->space = aspace_create();

    int status = process->space != NULL ? 0 : -ENOMEM;
    if (status == 0) {
        status = load_program(process, path, argc, argv);
    }
    if (status == 0) {
        status = setup_standard_files(process, parent);
    }
    if (status < 0) {
        destroy_unstarted(process);
        return status;
    }

    arch_irq_state_t irq_state = spinlock_acquire(&process_lock);
    int pid = next_pid++;
    process->pid = pid;
    list_push_back(&processes, &process->link);
    live_processes++;
    spinlock_release(&process_lock, irq_state);

    /* From the moment the thread exists it may run, exit, and be waited
     * for, so the process must be complete and listed before this. */
    struct thread *thread =
        thread_create_user(process->name, process_thread, process, aspace_arch(process->space), process);
    if (thread == NULL) {
        irq_state = spinlock_acquire(&process_lock);
        list_remove(&process->link);
        live_processes--;
        spinlock_release(&process_lock, irq_state);
        destroy_unstarted(process);
        return -ENOMEM;
    }
    thread_detach(thread);
    return pid;
}

_Noreturn void process_exit(int exit_code)
{
    struct process *process = process_current();
    if (process == NULL) {
        panic("process: a kernel thread called process_exit");
    }

    close_all_files(process);
    /* We are running on the address space we want to free. Step onto the
     * kernel's first; kernel code and this kernel stack exist in both. */
    thread_leave_address_space();
    aspace_destroy(process->space);
    process->space = NULL;

    arch_irq_state_t irq_state = spinlock_acquire(&process_lock);
    process->exit_code = exit_code;
    process->exited = true;
    bool forget = process->detached;
    if (forget) {
        list_remove(&process->link);
        live_processes--;
    } else {
        wait_queue_wake_all(&process->exit_waiters);
    }
    spinlock_release(&process_lock, irq_state);
    /* Unless detached, a waiter may free `process` from here on: do not
     * touch it again. */
    if (forget) {
        kfree(process);
    }

    thread_exit();
}

static struct process *find_process_locked(int pid)
{
    list_for_each (node, &processes) {
        struct process *process = container_of(node, struct process, link);
        if (process->pid == pid) {
            return process;
        }
    }
    return NULL;
}

int process_wait(int pid, int *exit_code)
{
    arch_irq_state_t irq_state = spinlock_acquire(&process_lock);

    struct process *process;
    for (;;) {
        /* Look the process up again after every sleep: another waiter may
         * have collected and freed it in the meantime. */
        process = find_process_locked(pid);
        if (process != NULL && process->detached) {
            process = NULL; /* its owner gave up the right to wait for it */
        }
        if (process == NULL || process->exited) {
            break;
        }
        wait_queue_wait(&process->exit_waiters, &process_lock);
    }
    if (process != NULL) {
        *exit_code = process->exit_code;
        list_remove(&process->link);
        live_processes--;
    }

    spinlock_release(&process_lock, irq_state);

    if (process == NULL) {
        return -ECHILD;
    }
    kfree(process);
    return 0;
}

int process_detach(int pid)
{
    arch_irq_state_t irq_state = spinlock_acquire(&process_lock);

    struct process *process = find_process_locked(pid);
    bool forget = false;
    if (process != NULL) {
        process->detached = true;
        /* Anyone already waiting must stop: there will be no exit code. */
        wait_queue_wake_all(&process->exit_waiters);
        if (process->exited) {
            /* Already over: nothing will come along to free it later. */
            list_remove(&process->link);
            live_processes--;
            forget = true;
        }
    }

    spinlock_release(&process_lock, irq_state);

    if (process == NULL) {
        return -ESRCH;
    }
    if (forget) {
        kfree(process);
    }
    return 0;
}

/* A CPU exception in user mode: the program is broken, the kernel is not. */
static void on_user_fault(const struct arch_trap_frame *frame, const struct arch_trap_info *info)
{
    struct process *process = process_current();
    if (process == NULL) {
        return; /* not ours: let the caller panic */
    }
    if (info->has_fault_address) {
        log_warn("process %d (%s): killed by %s at %p, address %p", process->pid, process->name, info->name,
                 (void *)arch_trap_pc(frame), (void *)info->fault_address);
    } else {
        log_warn("process %d (%s): killed by %s at %p", process->pid, process->name, info->name,
                 (void *)arch_trap_pc(frame));
    }
    process_exit(SD_EXIT_FAULT_BASE + (int)info->vector);
}

void process_init(void)
{
    panic_set_user_fault_handler(on_user_fault);
}
