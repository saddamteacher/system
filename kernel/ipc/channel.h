#ifndef IPC_CHANNEL_H
#define IPC_CHANNEL_H

#include <stddef.h>

/*
 * Message channels: a two-way connection between two endpoints, each of
 * which is an open file. What one end sends, the other receives, as whole
 * messages in order (never merged or split).
 *
 * A message may carry one open file along with its bytes. This is how a
 * process hands another a shared memory object, or one end of a new
 * channel.
 */

struct file;

/* Creates a connected pair. Returns 0 or -ENOMEM. */
int channel_create(struct file **first, struct file **second);

/*
 * Queues a message for the other end. Never blocks.
 *   -EMSGSIZE  larger than SD_MESSAGE_MAX
 *   -EPIPE     the other end is closed
 *   -EAGAIN    the other end has SD_CHANNEL_QUEUE_MAX messages waiting
 * `attachment` may be NULL; on success the message holds its own reference.
 */
int channel_send(struct file *end, const void *data, size_t size, struct file *attachment);

/*
 * Takes the oldest message, blocking until there is one. Returns its size,
 * or 0 once the other end is closed and everything was read.
 *   -EMSGSIZE  the message is larger than `capacity`; it stays queued
 * If the message carries a file, *attachment receives it (the caller then
 * owns that reference); otherwise *attachment is set to NULL. If
 * `attachment` is NULL, an attached file is dropped.
 */
long channel_receive(struct file *end, void *buffer, size_t capacity, struct file **attachment);

#endif
