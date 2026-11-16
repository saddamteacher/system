#include "user_memory.h"

#include <saddev/errno.h>

#include <core/string.h>
#include <mm/aspace.h>
#include <sched/process.h>

static bool range_valid(uint64_t address, size_t length, bool write)
{
    struct process *process = process_current();
    return process != NULL
        && aspace_user_range_valid(process_address_space(process), (uintptr_t)address, length, write);
}

bool user_range_readable(uint64_t address, size_t length)
{
    return range_valid(address, length, false);
}

bool user_range_writable(uint64_t address, size_t length)
{
    return range_valid(address, length, true);
}

/*
 * After validation the memory can be accessed directly: a system call runs
 * in the caller's address space. The check cannot go stale in between,
 * because only the process's single thread can change its mappings and
 * that thread is busy right here.
 */
int user_copy_in(void *destination, uint64_t user_source, size_t length)
{
    if (!user_range_readable(user_source, length)) {
        return -EFAULT;
    }
    memcpy(destination, (const void *)(uintptr_t)user_source, length);
    return 0;
}

int user_copy_out(uint64_t user_destination, const void *source, size_t length)
{
    if (!user_range_writable(user_destination, length)) {
        return -EFAULT;
    }
    memcpy((void *)(uintptr_t)user_destination, source, length);
    return 0;
}

int user_copy_string(char *destination, size_t capacity, uint64_t user_source)
{
    /* The string's length is unknown until its end is found, so validate
     * byte by byte: the terminator may sit right before an unmapped page. */
    for (size_t i = 0; i < capacity; i++) {
        if (!user_range_readable(user_source + i, 1)) {
            return -EFAULT;
        }
        destination[i] = *(const char *)(uintptr_t)(user_source + i);
        if (destination[i] == '\0') {
            return (int)i;
        }
    }
    return -ENAMETOOLONG;
}
