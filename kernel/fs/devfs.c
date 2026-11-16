#include <fs/devfs.h>

#include <saddev/errno.h>

#include <core/log.h>
#include <core/string.h>
#include <drivers/keyboard/keyboard.h>
#include <fs/vfs.h>

static int64_t console_write(struct file *file, const void *buffer, size_t size)
{
    (void)file;
    log_write_raw(buffer, size);
    return (int64_t)size;
}

static int64_t console_read(struct file *file, void *buffer, size_t size)
{
    (void)file;
    if (size == 0) {
        return 0;
    }
    /* Deliver one character per call: the reader gets each key as soon as
     * it is typed. Key releases and keys without a character are skipped. */
    for (;;) {
        struct key_event event;
        keyboard_wait_event(&event);
        if (event.pressed && event.character != 0) {
            *(char *)buffer = event.character;
            return 1;
        }
    }
}

static const struct file_ops console_ops = {
    .read = console_read,
    .write = console_write,
};

struct device {
    const char *name;
    const struct file_ops *ops;
};

static const struct device devices[] = {
    { "console", &console_ops },
};

#define DEVICE_COUNT (sizeof(devices) / sizeof(devices[0]))

/* `path` is "/" or "/<name>". */
static const struct device *find_device(const char *path)
{
    for (size_t i = 0; i < DEVICE_COUNT; i++) {
        if (strcmp(path + 1, devices[i].name) == 0) {
            return &devices[i];
        }
    }
    return NULL;
}

static bool is_root(const char *path)
{
    return path[1] == '\0';
}

static int devfs_open(void *context, const char *path, struct file *file)
{
    (void)context;
    if (is_root(path)) {
        return -EISDIR;
    }
    const struct device *device = find_device(path);
    if (device == NULL) {
        return -ENOENT;
    }
    file->ops = device->ops;
    file->type = SD_TYPE_DEVICE;
    return 0;
}

static int devfs_stat(void *context, const char *path, struct sd_stat *stat)
{
    (void)context;
    if (is_root(path)) {
        stat->type = SD_TYPE_DIRECTORY;
        return 0;
    }
    if (find_device(path) == NULL) {
        return -ENOENT;
    }
    stat->type = SD_TYPE_DEVICE;
    return 0;
}

static int devfs_readdir(void *context, const char *path, size_t index, struct sd_dirent *entry)
{
    (void)context;
    if (!is_root(path)) {
        return find_device(path) != NULL ? -ENOTDIR : -ENOENT;
    }
    if (index >= DEVICE_COUNT) {
        return -ENOENT;
    }
    entry->type = SD_TYPE_DEVICE;
    memcpy(entry->name, devices[index].name, strlen(devices[index].name) + 1);
    return 0;
}

static const struct filesystem devfs = {
    .name = "devfs",
    .open = devfs_open,
    .stat = devfs_stat,
    .readdir = devfs_readdir,
};

const struct filesystem *devfs_filesystem(void)
{
    return &devfs;
}
