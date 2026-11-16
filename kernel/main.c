/*
 * Portable kernel entry point and composition root: the one place that knows
 * about every subsystem and wires them together in the right order.
 */
#include <arch/boot.h>
#include <arch/cpu.h>
#include <arch/irq.h>
#include <core/cmdline.h>
#include <core/log.h>
#include <core/panic.h>
#include <core/time.h>
#include <drivers/keyboard/keyboard.h>
#include <fs/devfs.h>
#include <fs/tarfs.h>
#include <fs/vfs.h>
#include <drivers/serial/serial.h>
#include <mm/heap.h>
#include <mm/pmm.h>
#include <mm/vmm.h>
#include <sched/process.h>
#include <sched/thread.h>
#include <tests/tests.h>

#define BOOT_STACK_PAGES 16

static void run_requested_test(const char *cmdline)
{
    char name[32];
    if (!cmdline_get(cmdline, "test", name, sizeof(name))) {
        return;
    }

    log_info("test: running '%s'", name);
    switch (tests_run(name)) {
    case TEST_PASSED:
        log_info("test: '%s' passed", name);
        break;
    case TEST_FAILED:
        panic("test '%s' failed", name);
    case TEST_UNKNOWN:
        panic("unknown test '%s'", name);
    }
}

static void memory_init(void)
{
    /* Each layer is built on the one before it: frames, then pages mapped
     * onto frames, then arbitrary sizes carved out of pages. */
    pmm_init();
    vmm_init();
    heap_init();

    struct pmm_stats stats = pmm_stats();
    log_info("mm: %zu of %zu frames free after initialisation", stats.free_frames, stats.total_frames);
}

static void devices_init(void)
{
    const char *problem = arch_irq_init(vmm_map_physical);
    if (problem != NULL) {
        panic("irq: %s", problem);
    }
    time_init();
    if (!keyboard_init()) {
        log_warn("keyboard: no PS/2 keyboard found");
    }
}

/* Mounts the initial RAM file system (the first boot module) at "/" and
 * the device file system at "/dev". */
static void filesystems_init(void)
{
    if (arch_boot_module_count() == 0) {
        panic("boot: the bootloader did not load the initial file system");
    }
    struct arch_boot_module module = arch_boot_module(0);
    /* Module memory is not in the direct map. It stays mapped for good:
     * the file system serves file contents straight out of it. */
    const void *archive = vmm_map_physical(module.address, module.size);
    const struct filesystem *root = archive != NULL ? tarfs_create(archive, module.size) : NULL;
    if (root == NULL) {
        panic("boot: cannot use the initial file system '%s'", module.path);
    }
    if (vfs_mount("/", root) < 0 || vfs_mount("/dev", devfs_filesystem()) < 0) {
        panic("boot: cannot mount the file systems");
    }
}

static void start_detached(const char *name, thread_entry_fn entry)
{
    struct thread *thread = thread_create(name, entry, NULL);
    if (thread == NULL) {
        panic("boot: out of memory creating thread '%s'", name);
    }
    thread_detach(thread);
}

/* Starts the first user program and stays behind as its guardian. */
static void run_init(void)
{
    static const char *const argv[] = { "/bin/init" };
    int pid = process_spawn(argv[0], 1, argv, NULL);
    if (pid < 0) {
        panic("boot: cannot start /bin/init: error %d", -pid);
    }
    int exit_code = 0;
    process_wait(pid, &exit_code);
    /* Nothing would start programs any more; that is not a state to
     * continue in silently. */
    panic("init exited with code %d", exit_code);
}

/* The first real thread. Unlike the boot context, which becomes the idle
 * thread, this one is allowed to sleep and block. */
static void main_thread(void *argument)
{
    (void)argument;
    const char *cmdline = arch_boot_cmdline();
    log_info("boot: command line \"%s\"", cmdline);

    filesystems_init();
    process_init();
    log_info("hello from the kernel");

    char test_name[32];
    if (cmdline_get(cmdline, "test", test_name, sizeof(test_name))) {
        run_requested_test(cmdline);
        /* Ends the QEMU run of `make check`. */
        arch_debug_exit(ARCH_DEBUG_EXIT_SUCCESS);
        return;
    }
    run_init();
}

/* Second half of boot, running on a stack the kernel allocated itself. */
static void kmain_on_kernel_stack(void)
{
    devices_init();
    sched_init();
    /* Every handler is installed; from here on the kernel is interrupted,
     * and with the tick comes preemption. */
    arch_irq_enable();

    start_detached("main", main_thread);
    sched_idle_loop();
}

_Noreturn void kmain(void)
{
    /* Logging first: everything after this can report what it does. */
    serial_init();
    log_set_sink(serial_write);
    log_info("Saddev kernel starting");

    const char *problem = arch_boot_problem();
    if (problem != NULL) {
        panic("boot: %s", problem);
    }

    arch_cpu_init();
    log_info("cpu: descriptor tables and trap handlers installed");

    memory_init();

    /* Until now we ran on the bootloader's stack, which has no guard page
     * and lives in memory we want to reclaim eventually. */
    void *stack_top = vmm_alloc_kernel_stack(BOOT_STACK_PAGES);
    if (stack_top == NULL) {
        panic("boot: out of memory allocating the kernel stack");
    }
    arch_call_on_stack(stack_top, kmain_on_kernel_stack);
}
