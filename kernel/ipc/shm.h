#ifndef IPC_SHM_H
#define IPC_SHM_H

#include <stddef.h>
#include <stdint.h>

/*
 * Shared memory: a block of memory that several processes map at once. A
 * write by one is immediately visible to the others, with no copying and no
 * system call. The window server and its clients share window contents
 * this way.
 *
 * The object is an open file, so it can be sent over a channel. It lives
 * until the last file reference and the last mapping are gone.
 */

struct file;
struct address_space;

/* Creates a zero-filled object of `size` bytes (rounded up to whole pages).
 *   -EINVAL  size is 0 or above SD_SHM_MAX_SIZE
 *   -ENOMEM */
int shm_create(size_t size, struct file **file);

/* Maps the object into an address space; returns the address or 0 on
 * failure. Unmap with aspace_unmap() and the object's page count. */
uintptr_t shm_map(struct file *file, struct address_space *space);

/* True if `file` is a shared memory object. */
int shm_is_object(const struct file *file);

#endif
