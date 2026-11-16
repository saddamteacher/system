#include <unistd.h>

#include <errno.h>
#include <sys/syscall.h>

int errno;

/* The kernel reports errors as negative codes; C programs expect -1 and
 * the code in errno. */
static long check(long result)
{
    if (result < 0) {
        errno = (int)-result;
        return -1;
    }
    return result;
}

long write(int descriptor, const void *buffer, size_t size)
{
    return check(syscall6(SYS_WRITE, descriptor, (long)buffer, (long)size, 0, 0, 0));
}

long read(int descriptor, void *buffer, size_t size)
{
    return check(syscall6(SYS_READ, descriptor, (long)buffer, (long)size, 0, 0, 0));
}

int open(const char *path)
{
    return (int)check(syscall6(SYS_OPEN, (long)path, 0, 0, 0, 0, 0));
}

int close(int descriptor)
{
    return (int)check(syscall6(SYS_CLOSE, descriptor, 0, 0, 0, 0, 0));
}

long lseek(int descriptor, long offset, int whence)
{
    return check(syscall6(SYS_SEEK, descriptor, offset, whence, 0, 0, 0));
}

int stat(const char *path, struct sd_stat *result)
{
    return (int)check(syscall6(SYS_STAT, (long)path, (long)result, 0, 0, 0, 0));
}

int readdir_at(const char *path, size_t index, struct sd_dirent *entry)
{
    return (int)check(syscall6(SYS_READDIR, (long)path, (long)index, (long)entry, 0, 0, 0));
}

void *mmap_pages(size_t length)
{
    long result = check(syscall6(SYS_MMAP, (long)length, 0, 0, 0, 0, 0));
    return result == -1 ? NULL : (void *)result;
}

int munmap_pages(void *address, size_t length)
{
    return (int)check(syscall6(SYS_MUNMAP, (long)address, (long)length, 0, 0, 0, 0));
}

int spawn(const char *path, char *const argv[])
{
    return (int)check(syscall6(SYS_SPAWN, (long)path, (long)argv, 0, 0, 0, 0));
}

int waitpid(int pid, int *exit_code)
{
    return (int)check(syscall6(SYS_WAIT, pid, (long)exit_code, 0, 0, 0, 0));
}

int getpid(void)
{
    return (int)syscall6(SYS_GETPID, 0, 0, 0, 0, 0, 0);
}

int sleep_ms(uint64_t milliseconds)
{
    return (int)check(syscall6(SYS_SLEEP_MS, (long)milliseconds, 0, 0, 0, 0, 0));
}

int sched_yield(void)
{
    return (int)check(syscall6(SYS_YIELD, 0, 0, 0, 0, 0, 0));
}

uint64_t uptime_ms(void)
{
    return (uint64_t)syscall6(SYS_UPTIME_MS, 0, 0, 0, 0, 0, 0);
}

_Noreturn void _exit(int status)
{
    syscall6(SYS_EXIT, status, 0, 0, 0, 0, 0);
    /* The kernel never returns from exit; this only satisfies the compiler. */
    for (;;) {
    }
}
