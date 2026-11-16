/*
 * State: the mount table is filled during single-threaded boot and
 * read-only afterwards. An open file's offset is not locked: a file shared
 * by several threads behaves like a shared cursor, and callers that need
 * more must coordinate.
 */
#include <fs/vfs.h>

#include <saddev/errno.h>

#include <core/string.h>
#include <mm/heap.h>

#define MAX_MOUNTS 4

struct mount {
    const char *prefix;
    size_t prefix_length;
    const struct filesystem *filesystem;
};

static struct mount mounts[MAX_MOUNTS];
static size_t mount_count;

int vfs_mount(const char *prefix, const struct filesystem *filesystem)
{
    if (mount_count == MAX_MOUNTS) {
        return -ENOMEM;
    }
    mounts[mount_count++] = (struct mount){
        .prefix = prefix,
        .prefix_length = strlen(prefix),
        .filesystem = filesystem,
    };
    return 0;
}

/* "/dev" covers "/dev" and "/dev/x" but not "/device". */
static bool mount_covers(const struct mount *mount, const char *path)
{
    if (mount->prefix_length == 1) {
        return true; /* the root mount covers everything */
    }
    if (strncmp(path, mount->prefix, mount->prefix_length) != 0) {
        return false;
    }
    char next = path[mount->prefix_length];
    return next == '\0' || next == '/';
}

/*
 * Finds the file system responsible for `path` and the path as that file
 * system sees it. The longest matching prefix wins, so "/dev/console" goes
 * to the mount at "/dev", not to the root.
 */
static const struct filesystem *resolve(const char *path, const char **relative)
{
    if (path[0] != '/') {
        return NULL;
    }
    const struct mount *best = NULL;
    for (size_t i = 0; i < mount_count; i++) {
        if (mount_covers(&mounts[i], path) && (best == NULL || mounts[i].prefix_length > best->prefix_length)) {
            best = &mounts[i];
        }
    }
    if (best == NULL) {
        return NULL;
    }
    const char *rest = best->prefix_length == 1 ? path : path + best->prefix_length;
    *relative = rest[0] == '\0' ? "/" : rest;
    return best->filesystem;
}

int vfs_open(const char *path, struct file **result)
{
    const char *relative;
    const struct filesystem *filesystem = resolve(path, &relative);
    if (filesystem == NULL) {
        return -ENOENT;
    }
    struct file *file = kzalloc(sizeof(*file));
    if (file == NULL) {
        return -ENOMEM;
    }
    int status = filesystem->open(filesystem->context, relative, file);
    if (status < 0) {
        kfree(file);
        return status;
    }
    atomic_init(&file->references, 1);
    *result = file;
    return 0;
}

void vfs_retain(struct file *file)
{
    atomic_fetch_add(&file->references, 1);
}

void vfs_close(struct file *file)
{
    /* fetch_sub returns the old value: 1 means this was the last one. */
    if (atomic_fetch_sub(&file->references, 1) != 1) {
        return;
    }
    if (file->ops->close != NULL) {
        file->ops->close(file);
    }
    kfree(file);
}

int64_t vfs_read(struct file *file, void *buffer, size_t size)
{
    if (file->type == SD_TYPE_DIRECTORY) {
        return -EISDIR;
    }
    if (file->ops->read == NULL) {
        return -EINVAL;
    }
    int64_t count = file->ops->read(file, buffer, size);
    if (count > 0) {
        file->offset += (uint64_t)count;
    }
    return count;
}

int64_t vfs_write(struct file *file, const void *buffer, size_t size)
{
    if (file->type == SD_TYPE_DIRECTORY) {
        return -EISDIR;
    }
    if (file->ops->write == NULL) {
        return -EINVAL;
    }
    int64_t count = file->ops->write(file, buffer, size);
    if (count > 0) {
        file->offset += (uint64_t)count;
    }
    return count;
}

int64_t vfs_seek(struct file *file, int64_t offset, int whence)
{
    if (file->type != SD_TYPE_FILE) {
        return -EINVAL; /* a device is a stream: it has no position */
    }
    int64_t base;
    switch (whence) {
    case SD_SEEK_SET:
        base = 0;
        break;
    case SD_SEEK_CUR:
        base = (int64_t)file->offset;
        break;
    case SD_SEEK_END:
        base = (int64_t)file->size;
        break;
    default:
        return -EINVAL;
    }
    int64_t target = base + offset;
    if (target < 0 || (uint64_t)target > file->size) {
        return -EINVAL;
    }
    file->offset = (uint64_t)target;
    return target;
}

int vfs_stat(const char *path, struct sd_stat *stat)
{
    const char *relative;
    const struct filesystem *filesystem = resolve(path, &relative);
    if (filesystem == NULL) {
        return -ENOENT;
    }
    memset(stat, 0, sizeof(*stat));
    return filesystem->stat(filesystem->context, relative, stat);
}

int vfs_readdir(const char *path, size_t index, struct sd_dirent *entry)
{
    const char *relative;
    const struct filesystem *filesystem = resolve(path, &relative);
    if (filesystem == NULL) {
        return -ENOENT;
    }
    memset(entry, 0, sizeof(*entry));
    return filesystem->readdir(filesystem->context, relative, index, entry);
}
