/*
 * A channel is one object shared by its two ends. Each end has an inbox:
 * the queue of messages waiting to be received there.
 *
 * State: everything in struct channel is protected by channel->lock.
 * Lock order: channel lock, then the scheduler lock (wait queue). The poll
 * lock is never taken while holding a channel lock: poll_notify() is called
 * after releasing it.
 */
#include <ipc/channel.h>

#include <saddev/errno.h>
#include <saddev/syscall.h>

#include <core/list.h>
#include <core/spinlock.h>
#include <core/string.h>
#include <fs/poll.h>
#include <fs/vfs.h>
#include <mm/heap.h>
#include <sched/wait_queue.h>

struct message {
    struct list_node link;
    struct file *attachment; /* NULL if none; the message owns a reference */
    size_t size;
    unsigned char data[];
};

struct channel;

struct endpoint {
    struct channel *channel;
    struct endpoint *peer;
    struct list_node inbox;
    size_t inbox_count;
    bool closed;
    struct wait_queue receivers;
};

struct channel {
    struct spinlock lock;
    struct endpoint ends[2];
};

static void message_destroy(struct message *message)
{
    if (message->attachment != NULL) {
        vfs_close(message->attachment);
    }
    kfree(message);
}

int channel_send(struct file *file, const void *data, size_t size, struct file *attachment)
{
    struct endpoint *end = file->context;
    struct endpoint *peer = end->peer;

    if (size > SD_MESSAGE_MAX) {
        return -EMSGSIZE;
    }
    /* Build the message before taking the lock: allocation and copying do
     * not need it, and the lock is held with interrupts off. */
    struct message *message = kmalloc(sizeof(*message) + size);
    if (message == NULL) {
        return -ENOMEM;
    }
    message->size = size;
    message->attachment = attachment;
    memcpy(message->data, data, size);
    if (attachment != NULL) {
        vfs_retain(attachment);
    }

    arch_irq_state_t irq_state = spinlock_acquire(&end->channel->lock);
    int status = 0;
    if (peer->closed) {
        status = -EPIPE;
    } else if (peer->inbox_count == SD_CHANNEL_QUEUE_MAX) {
        /* The receiver is not keeping up. Report it instead of blocking: a
         * server must never hang because one client stopped reading. */
        status = -EAGAIN;
    } else {
        list_push_back(&peer->inbox, &message->link);
        peer->inbox_count++;
        wait_queue_wake_one(&peer->receivers);
    }
    spinlock_release(&end->channel->lock, irq_state);

    if (status < 0) {
        message_destroy(message);
        return status;
    }
    poll_notify();
    return 0;
}

long channel_receive(struct file *file, void *buffer, size_t capacity, struct file **attachment)
{
    struct endpoint *end = file->context;

    arch_irq_state_t irq_state = spinlock_acquire(&end->channel->lock);
    struct message *message = NULL;
    long result = 0;
    for (;;) {
        if (!list_is_empty(&end->inbox)) {
            message = container_of(end->inbox.next, struct message, link);
            if (message->size > capacity) {
                /* Leave it queued, so the caller can retry with a larger
                 * buffer instead of losing the message. */
                message = NULL;
                result = -EMSGSIZE;
            } else {
                list_remove(&message->link);
                end->inbox_count--;
            }
            break;
        }
        /* Checked after the inbox: messages sent before the peer closed
         * are still delivered. */
        if (end->peer->closed) {
            break;
        }
        wait_queue_wait(&end->receivers, &end->channel->lock);
    }
    spinlock_release(&end->channel->lock, irq_state);

    if (message == NULL) {
        if (attachment != NULL) {
            *attachment = NULL;
        }
        return result;
    }

    memcpy(buffer, message->data, message->size);
    result = (long)message->size;
    if (attachment != NULL) {
        /* Hand our reference over to the caller. */
        *attachment = message->attachment;
        message->attachment = NULL;
    }
    message_destroy(message);
    return result;
}

static int64_t channel_file_read(struct file *file, void *buffer, size_t size)
{
    return channel_receive(file, buffer, size, NULL);
}

static int64_t channel_file_write(struct file *file, const void *buffer, size_t size)
{
    int status = channel_send(file, buffer, size, NULL);
    return status < 0 ? status : (int64_t)size;
}

static unsigned channel_file_poll(struct file *file)
{
    struct endpoint *end = file->context;

    arch_irq_state_t irq_state = spinlock_acquire(&end->channel->lock);
    unsigned state = 0;
    if (!list_is_empty(&end->inbox)) {
        state |= SD_POLL_IN;
    }
    if (end->peer->closed) {
        /* Readable too: a receive returns immediately (with 0). */
        state |= SD_POLL_HUP | SD_POLL_IN;
    }
    spinlock_release(&end->channel->lock, irq_state);
    return state;
}

static void channel_file_close(struct file *file)
{
    struct endpoint *end = file->context;
    struct channel *channel = end->channel;
    struct list_node discarded;
    list_init(&discarded);

    arch_irq_state_t irq_state = spinlock_acquire(&channel->lock);
    end->closed = true;
    /* Nobody will ever receive what is still in our inbox. Move it aside
     * and free it after unlocking: closing an attachment can lead back
     * into channel code. */
    while (!list_is_empty(&end->inbox)) {
        struct list_node *node = end->inbox.next;
        list_remove(node);
        list_push_back(&discarded, node);
    }
    end->inbox_count = 0;
    bool both_closed = end->peer->closed;
    /* A peer blocked in receive must learn that nothing more will come. */
    wait_queue_wake_all(&end->peer->receivers);
    spinlock_release(&channel->lock, irq_state);

    while (!list_is_empty(&discarded)) {
        struct message *message = container_of(discarded.next, struct message, link);
        list_remove(&message->link);
        message_destroy(message);
    }
    if (both_closed) {
        kfree(channel);
    }
    poll_notify();
}

static const struct file_ops channel_ops = {
    .read = channel_file_read,
    .write = channel_file_write,
    .close = channel_file_close,
    .poll = channel_file_poll,
};

int channel_create(struct file **first, struct file **second)
{
    struct channel *channel = kzalloc(sizeof(*channel));
    if (channel == NULL) {
        return -ENOMEM;
    }
    channel->lock = (struct spinlock)SPINLOCK_INIT;
    for (int i = 0; i < 2; i++) {
        struct endpoint *end = &channel->ends[i];
        end->channel = channel;
        end->peer = &channel->ends[1 - i];
        list_init(&end->inbox);
        wait_queue_init(&end->receivers);
    }

    struct file *files[2];
    files[0] = vfs_file_create(&channel_ops, &channel->ends[0], SD_TYPE_CHANNEL);
    files[1] = vfs_file_create(&channel_ops, &channel->ends[1], SD_TYPE_CHANNEL);
    if (files[0] == NULL || files[1] == NULL) {
        /* No file was handed out, so nothing else can reference the
         * channel: free the pieces directly. */
        kfree(files[0]);
        kfree(files[1]);
        kfree(channel);
        return -ENOMEM;
    }
    *first = files[0];
    *second = files[1];
    return 0;
}
