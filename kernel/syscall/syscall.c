#include "syscall.h"

#include <stddef.h>

#include <saddev/errno.h>
#include <saddev/syscall.h>

#include <arch/user.h>

static const syscall_handler_fn handlers[SYS_COUNT] = {
    [SYS_EXIT] = sys_exit,
    [SYS_WRITE] = sys_write,
    [SYS_READ] = sys_read,
    [SYS_OPEN] = sys_open,
    [SYS_CLOSE] = sys_close,
    [SYS_SEEK] = sys_seek,
    [SYS_STAT] = sys_stat,
    [SYS_READDIR] = sys_readdir,
    [SYS_MMAP] = sys_mmap,
    [SYS_MUNMAP] = sys_munmap,
    [SYS_SPAWN] = sys_spawn,
    [SYS_WAIT] = sys_wait,
    [SYS_GETPID] = sys_getpid,
    [SYS_SLEEP_MS] = sys_sleep_ms,
    [SYS_YIELD] = sys_yield,
    [SYS_UPTIME_MS] = sys_uptime_ms,
};

int64_t kernel_syscall(uint64_t number, const uint64_t arguments[ARCH_SYSCALL_ARGUMENT_COUNT])
{
    /* The number comes from the program: never index with it unchecked. */
    if (number >= SYS_COUNT || handlers[number] == NULL) {
        return -ENOSYS;
    }
    return handlers[number](arguments);
}
