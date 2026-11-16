#ifndef _IPC_H
#define _IPC_H

#include <stddef.h>
#include <stdint.h>

#include <saddev/syscall.h>

/*
 * Inter-process communication: message channels, named ports, shared
 * memory, and waiting on several descriptors. Functions return -1 and set
 * errno on failure unless noted.
 */

/* Creates a connected pair of channel ends in this process. */
int channel_create(int descriptors[2]);

/* Server side: publish a name, then accept connections on it. */
int port_create(const char *name);
int port_accept(int port);
/* Client side: returns one end of a new channel to the server. */
int port_connect(const char *name);

/*
 * Sends one message, optionally with a descriptor attached (SD_NO_HANDLE
 * for none). Never blocks: fails with EAGAIN if the receiver's queue is
 * full, EPIPE if the other end is closed.
 */
int msg_send(int channel, const void *message, size_t size, int handle);

/*
 * Receives one message, blocking until there is one. Returns its size, or
 * 0 when the other end is closed and nothing is left. If `handle` is not
 * NULL it receives an attached descriptor, or SD_NO_HANDLE.
 */
long msg_recv(int channel, void *buffer, size_t capacity, int *handle);

/* Waits until a descriptor is ready; returns how many are, 0 on timeout.
 * timeout_ms < 0 waits forever. */
int poll_fds(struct sd_pollfd *descriptors, size_t count, int64_t timeout_ms);

/* Creates a shared memory object; returns its descriptor. */
int shm_create(size_t size);
/* Maps a shared memory object; returns NULL on failure. If `size` is not
 * NULL it receives the object's size in bytes. */
void *shm_map(int descriptor, size_t *size);
/* Removes a mapping made by shm_map(). */
int shm_unmap(void *address, size_t size);

int fstat(int descriptor, struct sd_stat *result);

#endif
