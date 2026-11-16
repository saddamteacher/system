#ifndef FS_VFS_H
#define FS_VFS_H

#include <stdatomic.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include <saddev/syscall.h>

/*
 * Virtual file system: one interface (open, read, write, ...) in front of
 * different kinds of file systems, selected by path prefix.
 *
 * Paths are absolute, components separated by single slashes, no "." or
 * "..". Functions return 0 or a positive count on success and a negative
 * error code from <saddev/errno.h> on failure.
 */

struct file;

/* What a file system provides for an open file. Any member may be NULL,
 * which makes the operation fail with -EINVAL (or do nothing, for close). */
struct file_ops {
    /* Transfer up to `size` bytes at file->offset; return the count. They
     * do not advance the offset: the VFS does. */
    int64_t (*read)(struct file *file, void *buffer, size_t size);
    int64_t (*write)(struct file *file, const void *buffer, size_t size);
    void (*close)(struct file *file);
    /* Returns SD_POLL_* bits describing the file right now, without
     * blocking. NULL means "always readable". */
    unsigned (*poll)(struct file *file);
};

/* An open file. Shared between everyone who holds a reference to it,
 * including the read/write position. */
struct file {
    const struct file_ops *ops;
    void *context;       /* owned by the file system */
    uint32_t type;       /* SD_TYPE_* */
    uint64_t size;       /* for regular files */
    uint64_t offset;
    atomic_uint references;
};

/* What a file system provides for paths. `path` is relative to the mount
 * point and always starts with '/'. */
struct filesystem {
    const char *name;
    void *context;
    /* Fills in ops, context, type and size of `file`. */
    int (*open)(void *context, const char *path, struct file *file);
    int (*stat)(void *context, const char *path, struct sd_stat *stat);
    /* Stores the index-th entry of a directory; -ENOENT past the end. */
    int (*readdir)(void *context, const char *path, size_t index, struct sd_dirent *entry);
};

/* Attaches a file system at `prefix` ("/" or e.g. "/dev"). The structure
 * must stay valid forever. Call during boot only. */
int vfs_mount(const char *prefix, const struct filesystem *filesystem);

int vfs_open(const char *path, struct file **file);
/* Creates an open file that has no path: IPC objects use the file interface
 * so that they can live in descriptor tables. Returns NULL if out of
 * memory. */
struct file *vfs_file_create(const struct file_ops *ops, void *context, uint32_t type);
/* Adds a reference; each reference is dropped with vfs_close(). */
void vfs_retain(struct file *file);
void vfs_close(struct file *file);

int64_t vfs_read(struct file *file, void *buffer, size_t size);
int64_t vfs_write(struct file *file, const void *buffer, size_t size);
int64_t vfs_seek(struct file *file, int64_t offset, int whence);

int vfs_stat(const char *path, struct sd_stat *stat);
void vfs_fstat(const struct file *file, struct sd_stat *stat);
/* Current SD_POLL_* state of a file. */
unsigned vfs_poll(struct file *file);
int vfs_readdir(const char *path, size_t index, struct sd_dirent *entry);

#endif
