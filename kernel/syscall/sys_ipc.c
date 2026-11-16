#include "syscall.h"

#include <saddev/errno.h>
#include <saddev/syscall.h>

#include <drivers/framebuffer/framebuffer.h>
#include <fs/poll.h>
#include <fs/vfs.h>
#include <ipc/channel.h>
#include <ipc/port.h>
#include <ipc/shm.h>
#include <mm/heap.h>
#include <sched/process.h>

#include "user_memory.h"

static struct file *file_of_type(uint64_t descriptor, uint32_t type)
{
    if (descriptor >= PROCESS_MAX_FILES) {
        return NULL;
    }
    struct file *file = process_file_get(process_current(), (int)descriptor);
    return file != NULL && file->type == type ? file : NULL;
}

/* Puts a new file into the caller's descriptor table; on failure the file
 * is closed, so the caller's reference is consumed either way. */
static int64_t install(struct file *file)
{
    int descriptor = process_file_install(process_current(), file);
    if (descriptor < 0) {
        vfs_close(file);
    }
    return descriptor;
}

int64_t sys_fstat(const uint64_t arguments[])
{
    if (arguments[0] >= PROCESS_MAX_FILES) {
        return -EBADF;
    }
    struct file *file = process_file_get(process_current(), (int)arguments[0]);
    if (file == NULL) {
        return -EBADF;
    }
    struct sd_stat stat;
    vfs_fstat(file, &stat);
    return user_copy_out(arguments[1], &stat, sizeof(stat));
}

int64_t sys_poll(const uint64_t arguments[])
{
    uint64_t user_entries = arguments[0];
    size_t count = arguments[1];
    int64_t timeout_ms = (int64_t)arguments[2];

    if (count > SD_POLL_MAX) {
        return -EINVAL;
    }
    struct sd_pollfd requests[SD_POLL_MAX];
    struct poll_entry entries[SD_POLL_MAX];
    int status = user_copy_in(requests, user_entries, count * sizeof(requests[0]));
    if (status < 0) {
        return status;
    }
    struct process *process = process_current();
    for (size_t i = 0; i < count; i++) {
        /* An invalid descriptor is simply never ready, as in POSIX, so one
         * stale entry does not make the whole call fail. */
        bool valid = requests[i].fd >= 0 && requests[i].fd < PROCESS_MAX_FILES;
        entries[i].file = valid ? process_file_get(process, requests[i].fd) : NULL;
        entries[i].events = requests[i].events;
    }

    int ready = poll_wait(entries, count, timeout_ms);

    for (size_t i = 0; i < count; i++) {
        requests[i].revents = entries[i].revents;
    }
    status = user_copy_out(user_entries, requests, count * sizeof(requests[0]));
    return status < 0 ? status : ready;
}

int64_t sys_channel_create(const uint64_t arguments[])
{
    if (!user_range_writable(arguments[0], 2 * sizeof(int))) {
        return -EFAULT;
    }
    struct file *first, *second;
    int status = channel_create(&first, &second);
    if (status < 0) {
        return status;
    }
    struct process *process = process_current();
    int descriptors[2];
    descriptors[0] = process_file_install(process, first);
    descriptors[1] = descriptors[0] >= 0 ? process_file_install(process, second) : -EMFILE;
    if (descriptors[1] < 0) {
        /* Undo whatever was installed; the files are closed either way. */
        if (descriptors[0] >= 0) {
            process_file_remove(process, descriptors[0]);
        } else {
            vfs_close(first);
        }
        vfs_close(second);
        return -EMFILE;
    }
    return user_copy_out(arguments[0], descriptors, sizeof(descriptors));
}

static int copy_port_name(char *name, uint64_t user_name)
{
    /* One more than the limit, so a name that is too long is detected
     * rather than silently cut. */
    return user_copy_string(name, SD_PORT_NAME_MAX + 2, user_name);
}

int64_t sys_port_create(const uint64_t arguments[])
{
    char name[SD_PORT_NAME_MAX + 2];
    int length = copy_port_name(name, arguments[0]);
    if (length < 0) {
        return length;
    }
    struct file *port;
    int status = port_create(name, &port);
    return status < 0 ? status : install(port);
}

int64_t sys_port_connect(const uint64_t arguments[])
{
    char name[SD_PORT_NAME_MAX + 2];
    int length = copy_port_name(name, arguments[0]);
    if (length < 0) {
        return length;
    }
    struct file *channel_end;
    int status = port_connect(name, &channel_end);
    return status < 0 ? status : install(channel_end);
}

int64_t sys_port_accept(const uint64_t arguments[])
{
    struct file *port = file_of_type(arguments[0], SD_TYPE_PORT);
    if (port == NULL) {
        return -EBADF;
    }
    struct file *channel_end;
    int status = port_accept(port, &channel_end);
    return status < 0 ? status : install(channel_end);
}

int64_t sys_send(const uint64_t arguments[])
{
    struct file *channel = file_of_type(arguments[0], SD_TYPE_CHANNEL);
    uint64_t buffer = arguments[1];
    size_t size = arguments[2];
    int64_t handle = (int64_t)arguments[3];

    if (channel == NULL) {
        return -EBADF;
    }
    if (size > SD_MESSAGE_MAX) {
        return -EMSGSIZE;
    }
    if (!user_range_readable(buffer, size)) {
        return -EFAULT;
    }
    struct file *attachment = NULL;
    if (handle != SD_NO_HANDLE) {
        if (handle < 0 || handle >= PROCESS_MAX_FILES) {
            return -EBADF;
        }
        attachment = process_file_get(process_current(), (int)handle);
        if (attachment == NULL) {
            return -EBADF;
        }
    }
    /* The sender keeps its own descriptor; the message gets a reference of
     * its own. */
    return channel_send(channel, (const void *)(uintptr_t)buffer, size, attachment);
}

int64_t sys_recv(const uint64_t arguments[])
{
    struct file *channel = file_of_type(arguments[0], SD_TYPE_CHANNEL);
    uint64_t buffer = arguments[1];
    size_t capacity = arguments[2] > SD_MESSAGE_MAX ? SD_MESSAGE_MAX : arguments[2];
    uint64_t user_handle = arguments[3];

    if (channel == NULL) {
        return -EBADF;
    }
    if (!user_range_writable(buffer, capacity)
        || (user_handle != 0 && !user_range_writable(user_handle, sizeof(int)))) {
        return -EFAULT;
    }

    struct file *attachment = NULL;
    long size = channel_receive(channel, (void *)(uintptr_t)buffer, capacity, &attachment);
    if (size < 0) {
        return size;
    }
    int handle = SD_NO_HANDLE;
    if (attachment != NULL) {
        /* A receiver that did not ask for the file, or has no room for it,
         * still gets the message; the file is closed. */
        if (user_handle != 0) {
            handle = process_file_install(process_current(), attachment);
        }
        if (handle < 0) {
            vfs_close(attachment);
            handle = SD_NO_HANDLE;
        }
    }
    if (user_handle != 0) {
        /* Cannot fail: the range was validated above. */
        user_copy_out(user_handle, &handle, sizeof(handle));
    }
    return size;
}

int64_t sys_shm_create(const uint64_t arguments[])
{
    struct file *file;
    int status = shm_create(arguments[0], &file);
    return status < 0 ? status : install(file);
}

int64_t sys_shm_map(const uint64_t arguments[])
{
    struct file *file = file_of_type(arguments[0], SD_TYPE_SHM);
    if (file == NULL || !shm_is_object(file)) {
        return -EBADF;
    }
    uintptr_t address = shm_map(file, process_address_space(process_current()));
    return address != 0 ? (int64_t)address : -ENOMEM;
}

int64_t sys_fb_info(const uint64_t arguments[])
{
    struct sd_fb_info info;
    if (!framebuffer_info(&info)) {
        return -ENODEV;
    }
    return user_copy_out(arguments[0], &info, sizeof(info));
}

int64_t sys_fb_map(const uint64_t arguments[])
{
    (void)arguments;
    int error = 0;
    uintptr_t address = framebuffer_map(process_address_space(process_current()), &error);
    return address != 0 ? (int64_t)address : error;
}
