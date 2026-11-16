#include <fs/devfs.h>

#include <saddev/errno.h>

#include <core/log.h>
#include <core/string.h>
#include <drivers/keyboard/keyboard.h>
#include <drivers/mouse/mouse.h>
#include <fs/poll.h>
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
        struct sd_key_event event;
        keyboard_wait_event(&event);
        if (event.pressed && event.character != 0) {
            *(char *)buffer = event.character;
            return 1;
        }
    }
}

static unsigned console_poll(struct file *file)
{
    (void)file;
    /* An approximation: a queued event may turn out to be a key release,
     * for which read() keeps waiting. Good enough for "is someone typing". */
    return keyboard_has_event() ? SD_POLL_IN : 0;
}

static const struct file_ops console_ops = {
    .read = console_read,
    .write = console_write,
    .poll = console_poll,
};

/*
 * Event devices: a read returns whole event structures, as many as are
 * pending and fit, and blocks while there are none.
 */
static int64_t read_events(struct file *file, void *buffer, size_t size, size_t event_size,
                           bool (*take)(void *event))
{
    if (size < event_size) {
        return -EINVAL;
    }
    size_t count = 0;
    for (;;) {
        while ((count + 1) * event_size <= size && take((char *)buffer + count * event_size)) {
            count++;
        }
        if (count > 0) {
            return (int64_t)(count * event_size);
        }
        /* Sleep until this device reports input, then look again. */
        struct poll_entry entry = { .file = file, .events = SD_POLL_IN };
        poll_wait(&entry, 1, -1);
    }
}

static bool take_key_event(void *event)
{
    return keyboard_read_event(event);
}

static bool take_mouse_event(void *event)
{
    return mouse_read_event(event);
}

static int64_t keyboard_device_read(struct file *file, void *buffer, size_t size)
{
    return read_events(file, buffer, size, sizeof(struct sd_key_event), take_key_event);
}

static int64_t mouse_device_read(struct file *file, void *buffer, size_t size)
{
    return read_events(file, buffer, size, sizeof(struct sd_mouse_event), take_mouse_event);
}

static unsigned keyboard_device_poll(struct file *file)
{
    (void)file;
    return keyboard_has_event() ? SD_POLL_IN : 0;
}

static unsigned mouse_device_poll(struct file *file)
{
    (void)file;
    return mouse_has_event() ? SD_POLL_IN : 0;
}

static const struct file_ops keyboard_device_ops = {
    .read = keyboard_device_read,
    .poll = keyboard_device_poll,
};

static const struct file_ops mouse_device_ops = {
    .read = mouse_device_read,
    .poll = mouse_device_poll,
};

struct device {
    const char *name;
    const struct file_ops *ops;
};

static const struct device devices[] = {
    { "console", &console_ops },
    { "keyboard", &keyboard_device_ops },
    { "mouse", &mouse_device_ops },
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
    /* Pollers of the console must hear about new input. */
    keyboard_set_listener(poll_notify);
    mouse_set_listener(poll_notify);
    return &devfs;
}
