#include "syscall.h"

#include <saddev/errno.h>
#include <saddev/syscall.h>

#include <fs/vfs.h>
#include <sched/process.h>

#include "user_memory.h"

/* Largest single transfer. Keeps byte counts well inside int64_t and bounds
 * the time one call can spend in the kernel. */
#define MAX_TRANSFER (1u << 20)

static struct file *file_for(uint64_t descriptor)
{
    /* Compare as unsigned: a "negative" descriptor is a huge number here. */
    if (descriptor >= PROCESS_MAX_FILES) {
        return NULL;
    }
    return process_file_get(process_current(), (int)descriptor);
}

int64_t sys_write(const uint64_t arguments[])
{
    struct file *file = file_for(arguments[0]);
    uint64_t buffer = arguments[1];
    size_t size = arguments[2] > MAX_TRANSFER ? MAX_TRANSFER : arguments[2];

    if (file == NULL) {
        return -EBADF;
    }
    if (!user_range_readable(buffer, size)) {
        return -EFAULT;
    }
    return vfs_write(file, (const void *)(uintptr_t)buffer, size);
}

int64_t sys_read(const uint64_t arguments[])
{
    struct file *file = file_for(arguments[0]);
    uint64_t buffer = arguments[1];
    size_t size = arguments[2] > MAX_TRANSFER ? MAX_TRANSFER : arguments[2];

    if (file == NULL) {
        return -EBADF;
    }
    if (!user_range_writable(buffer, size)) {
        return -EFAULT;
    }
    return vfs_read(file, (void *)(uintptr_t)buffer, size);
}

int64_t sys_open(const uint64_t arguments[])
{
    char path[SD_PATH_MAX + 1];
    int length = user_copy_string(path, sizeof(path), arguments[0]);
    if (length < 0) {
        return length;
    }
    struct file *file;
    int status = vfs_open(path, &file);
    if (status < 0) {
        return status;
    }
    int descriptor = process_file_install(process_current(), file);
    if (descriptor < 0) {
        vfs_close(file);
    }
    return descriptor;
}

int64_t sys_close(const uint64_t arguments[])
{
    if (arguments[0] >= PROCESS_MAX_FILES) {
        return -EBADF;
    }
    return process_file_remove(process_current(), (int)arguments[0]);
}

int64_t sys_seek(const uint64_t arguments[])
{
    struct file *file = file_for(arguments[0]);
    if (file == NULL) {
        return -EBADF;
    }
    return vfs_seek(file, (int64_t)arguments[1], (int)arguments[2]);
}

int64_t sys_stat(const uint64_t arguments[])
{
    char path[SD_PATH_MAX + 1];
    int length = user_copy_string(path, sizeof(path), arguments[0]);
    if (length < 0) {
        return length;
    }
    /* Fill a kernel copy and hand it out only when everything succeeded,
     * so the program never sees a half-written structure. */
    struct sd_stat stat;
    int status = vfs_stat(path, &stat);
    if (status < 0) {
        return status;
    }
    return user_copy_out(arguments[1], &stat, sizeof(stat));
}

int64_t sys_readdir(const uint64_t arguments[])
{
    char path[SD_PATH_MAX + 1];
    int length = user_copy_string(path, sizeof(path), arguments[0]);
    if (length < 0) {
        return length;
    }
    struct sd_dirent entry;
    int status = vfs_readdir(path, arguments[1], &entry);
    if (status < 0) {
        return status;
    }
    return user_copy_out(arguments[2], &entry, sizeof(entry));
}
