#ifndef _UNISTD_H
#define _UNISTD_H

#include <stddef.h>
#include <stdint.h>

#include <saddev/syscall.h>

#define STDIN_FILENO  0
#define STDOUT_FILENO 1
#define STDERR_FILENO 2

#define SEEK_SET SD_SEEK_SET
#define SEEK_CUR SD_SEEK_CUR
#define SEEK_END SD_SEEK_END

/*
 * Thin wrappers around system calls. On failure they return -1 and set
 * errno, as C programs expect.
 */
long write(int descriptor, const void *buffer, size_t size);
long read(int descriptor, void *buffer, size_t size);
int open(const char *path);
int close(int descriptor);
long lseek(int descriptor, long offset, int whence);
int stat(const char *path, struct sd_stat *result);
/* Stores the index-th entry of a directory; fails with ENOENT past the end. */
int readdir_at(const char *path, size_t index, struct sd_dirent *entry);

/* Zero-filled memory in whole pages, straight from the kernel. */
void *mmap_pages(size_t length);
int munmap_pages(void *address, size_t length);

/* Starts a program. argv is NULL-terminated; argv[0] is its name by
 * convention. Returns the new process id. */
int spawn(const char *path, char *const argv[]);
/* Waits for a process to end; stores its exit code if exit_code != NULL. */
int waitpid(int pid, int *exit_code);
/* Declares that this program will not wait for process `pid`; the system
 * then cleans up after it by itself. */
int detach(int pid);
int getpid(void);

int sleep_ms(uint64_t milliseconds);
int sched_yield(void);
uint64_t uptime_ms(void);
/* Nanoseconds since boot, with sub-microsecond resolution. */
uint64_t clock_ns(void);
/* Calendar time: seconds since 1970-01-01 00:00:00 UTC. */
uint64_t wall_time(void);

_Noreturn void _exit(int status);

#endif
