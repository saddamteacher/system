#ifndef SADDEV_SYSCALL_H
#define SADDEV_SYSCALL_H

/*
 * The system call interface: the only way a user program talks to the
 * kernel. This header is shared by the kernel and the C library.
 *
 * Calling convention (x86_64): number in rax, arguments in rdi, rsi, rdx,
 * r10, r8, r9, `syscall` instruction, result in rax. A negative result is
 * an error code from <saddev/errno.h>.
 */

#include <stdint.h>

enum {
    SYS_EXIT = 0,   /* (int code)                               never returns */
    SYS_WRITE,      /* (int fd, const void *buf, size_t n)      -> bytes written */
    SYS_READ,       /* (int fd, void *buf, size_t n)            -> bytes read, 0 at end */
    SYS_OPEN,       /* (const char *path)                       -> fd */
    SYS_CLOSE,      /* (int fd)                                 -> 0 */
    SYS_SEEK,       /* (int fd, int64_t offset, int whence)     -> new offset */
    SYS_STAT,       /* (const char *path, struct sd_stat *out)  -> 0 */
    SYS_READDIR,    /* (const char *path, size_t index, struct sd_dirent *out) -> 0 */
    SYS_MMAP,       /* (size_t length)                          -> address of zeroed memory */
    SYS_MUNMAP,     /* (void *address, size_t length)           -> 0 */
    SYS_SPAWN,      /* (const char *path, char *const argv[])   -> pid */
    SYS_WAIT,       /* (int pid, int *exit_code)                -> 0 */
    SYS_GETPID,     /* ()                                       -> pid */
    SYS_SLEEP_MS,   /* (uint64_t milliseconds)                  -> 0 */
    SYS_YIELD,      /* ()                                       -> 0 */
    SYS_UPTIME_MS,  /* ()                                       -> milliseconds since boot */
    SYS_COUNT
};

/* Values for the `whence` argument of SYS_SEEK. */
#define SD_SEEK_SET 0 /* from the start */
#define SD_SEEK_CUR 1 /* from the current position */
#define SD_SEEK_END 2 /* from the end */

#define SD_TYPE_FILE      1
#define SD_TYPE_DIRECTORY 2
#define SD_TYPE_DEVICE    3

struct sd_stat {
    uint32_t type; /* SD_TYPE_* */
    uint32_t reserved;
    uint64_t size; /* bytes; 0 for directories and devices */
};

#define SD_NAME_MAX 59

struct sd_dirent {
    uint32_t type; /* SD_TYPE_* */
    char name[SD_NAME_MAX + 1];
};

/* Limits of SYS_SPAWN. */
#define SD_PATH_MAX 255
#define SD_ARG_MAX  16  /* arguments per program */
#define SD_ARG_LENGTH_MAX 255

/* Exit code of a process the kernel ended because of a CPU exception:
 * SD_EXIT_FAULT_BASE + exception vector (14 = page fault). */
#define SD_EXIT_FAULT_BASE 128

#endif
