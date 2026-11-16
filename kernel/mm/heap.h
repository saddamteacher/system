#ifndef MM_HEAP_H
#define MM_HEAP_H

#include <stddef.h>

/*
 * Kernel heap: memory in arbitrary sizes. Safe to call from any context.
 * Returned memory is aligned to 16 bytes.
 */

/* Requires vmm_init(). Call once. */
void heap_init(void);

/* Returns NULL if `size` is 0 or memory is exhausted. Contents are
 * unspecified; use kzalloc() for zeroed memory. */
void *kmalloc(size_t size);
void *kzalloc(size_t size);

/* kfree(NULL) does nothing. Freeing a pointer twice, or one that did not
 * come from kmalloc(), is a kernel bug and panics when detected. */
void kfree(void *pointer);

struct heap_stats {
    size_t used_bytes;  /* allocated blocks, including their headers */
    size_t arena_bytes; /* memory the heap holds from the vmm */
    size_t arena_count;
};

struct heap_stats heap_stats(void);

#endif
