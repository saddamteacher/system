#include "syscall.h"

#include <saddev/errno.h>
#include <saddev/syscall.h>

#include <arch/mmu.h>
#include <core/time.h>
#include <mm/aspace.h>
#include <mm/heap.h>
#include <sched/process.h>
#include <sched/thread.h>

#include "user_memory.h"

/* Upper bound for one mmap call: 1 GiB. */
#define MAX_MMAP_PAGES (1u << 18)

int64_t sys_exit(const uint64_t arguments[])
{
    process_exit((int)arguments[0]);
}

static size_t pages_for(uint64_t length)
{
    return (length + ARCH_PAGE_SIZE - 1) / ARCH_PAGE_SIZE;
}

int64_t sys_mmap(const uint64_t arguments[])
{
    uint64_t length = arguments[0];
    if (length == 0 || length > (uint64_t)MAX_MMAP_PAGES * ARCH_PAGE_SIZE) {
        return -EINVAL;
    }
    struct address_space *space = process_address_space(process_current());
    uintptr_t address = aspace_map_anywhere(space, pages_for(length), ARCH_MAP_WRITE);
    return address != 0 ? (int64_t)address : -ENOMEM;
}

int64_t sys_munmap(const uint64_t arguments[])
{
    uint64_t length = arguments[1];
    if (length == 0 || length > (uint64_t)MAX_MMAP_PAGES * ARCH_PAGE_SIZE) {
        return -EINVAL;
    }
    struct address_space *space = process_address_space(process_current());
    return aspace_unmap(space, (uintptr_t)arguments[0], pages_for(length)) ? 0 : -EINVAL;
}

/*
 * Copies a program's argv array into kernel memory: `storage` receives the
 * strings back to back, `pointers` the start of each. Returns the argument
 * count or a negative error code.
 */
static int copy_arguments(uint64_t user_argv, char *storage, size_t storage_size, const char *pointers[])
{
    if (user_argv == 0) {
        return 0;
    }
    size_t used = 0;
    for (int count = 0; count <= SD_ARG_MAX; count++) {
        uint64_t user_string;
        int status = user_copy_in(&user_string, user_argv + (uint64_t)count * sizeof(uint64_t), sizeof(user_string));
        if (status < 0) {
            return status;
        }
        if (user_string == 0) {
            return count; /* the NULL that ends argv */
        }
        if (count == SD_ARG_MAX) {
            break;
        }
        size_t room = storage_size - used;
        if (room > SD_ARG_LENGTH_MAX + 1) {
            room = SD_ARG_LENGTH_MAX + 1;
        }
        int length = user_copy_string(storage + used, room, user_string);
        if (length < 0) {
            return length;
        }
        pointers[count] = storage + used;
        used += (size_t)length + 1;
    }
    return -EINVAL; /* too many arguments */
}

int64_t sys_spawn(const uint64_t arguments[])
{
    char path[SD_PATH_MAX + 1];
    int length = user_copy_string(path, sizeof(path), arguments[0]);
    if (length < 0) {
        return length;
    }

    /* Too big for the kernel stack. */
    size_t storage_size = SD_ARG_MAX * (SD_ARG_LENGTH_MAX + 1);
    char *storage = kmalloc(storage_size);
    if (storage == NULL) {
        return -ENOMEM;
    }
    const char *argv[SD_ARG_MAX];
    int argc = copy_arguments(arguments[1], storage, storage_size, argv);
    int64_t result = argc < 0 ? argc : process_spawn(path, argc, argv, process_current());

    kfree(storage);
    return result;
}

int64_t sys_wait(const uint64_t arguments[])
{
    /* Check the destination before blocking: failing after the wait would
     * lose the exit code. */
    if (arguments[1] != 0 && !user_range_writable(arguments[1], sizeof(int))) {
        return -EFAULT;
    }
    int exit_code;
    int status = process_wait((int)arguments[0], &exit_code);
    if (status < 0) {
        return status;
    }
    if (arguments[1] != 0) {
        return user_copy_out(arguments[1], &exit_code, sizeof(exit_code));
    }
    return 0;
}

int64_t sys_getpid(const uint64_t arguments[])
{
    (void)arguments;
    return process_id(process_current());
}

int64_t sys_sleep_ms(const uint64_t arguments[])
{
    thread_sleep_ms(arguments[0]);
    return 0;
}

int64_t sys_yield(const uint64_t arguments[])
{
    (void)arguments;
    thread_yield();
    return 0;
}

int64_t sys_uptime_ms(const uint64_t arguments[])
{
    (void)arguments;
    return (int64_t)time_now_ms();
}

int64_t sys_clock_ns(const uint64_t arguments[])
{
    (void)arguments;
    return (int64_t)time_now_ns();
}

int64_t sys_wall_time(const uint64_t arguments[])
{
    (void)arguments;
    return (int64_t)time_wall_seconds();
}

int64_t sys_detach(const uint64_t arguments[])
{
    return process_detach((int)arguments[0]);
}
