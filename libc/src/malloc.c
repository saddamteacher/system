/*
 * Heap allocator for user programs.
 *
 * Same design as the kernel heap (kernel/mm/heap.c), written independently
 * for user space: memory comes from the kernel in page-sized arenas through
 * mmap_pages(); arenas are tiled by blocks with boundary tags; free blocks
 * are kept on a first-fit list and merged with free neighbours.
 *
 * No locking: a program has one thread.
 */
#include <stdlib.h>

#include <stdint.h>
#include <string.h>
#include <unistd.h>

#define PAGE_SIZE 4096UL
#define ALIGNMENT 16
#define DEFAULT_ARENA_SIZE (64 * 1024UL)
#define BLOCK_USED 1UL

struct block {
    size_t size_and_flags; /* whole block, header included */
    size_t previous_size;  /* 0 for the first block of an arena */
};

struct free_link {
    struct free_link *next;
    struct free_link *previous;
};

struct arena {
    size_t size;
    size_t padding;
};

#define MIN_BLOCK_SIZE (sizeof(struct block) + sizeof(struct free_link))
#define ARENA_OVERHEAD (sizeof(struct arena) + sizeof(struct block))

static struct free_link free_list = { &free_list, &free_list };
static size_t arena_count;
static size_t bytes_in_use;

size_t malloc_bytes_in_use(void)
{
    return bytes_in_use;
}

static size_t block_size(const struct block *block)
{
    return block->size_and_flags & ~BLOCK_USED;
}

static int block_is_used(const struct block *block)
{
    return (block->size_and_flags & BLOCK_USED) != 0;
}

static struct block *next_block(struct block *block)
{
    return (struct block *)((char *)block + block_size(block));
}

static struct free_link *block_link(struct block *block)
{
    return (struct free_link *)((char *)block + sizeof(struct block));
}

static struct block *block_from_link(struct free_link *link)
{
    return (struct block *)((char *)link - sizeof(struct block));
}

static void list_remove(struct free_link *link)
{
    link->previous->next = link->next;
    link->next->previous = link->previous;
}

static void make_free_block(struct block *block, size_t size)
{
    block->size_and_flags = size;
    next_block(block)->previous_size = size;

    struct free_link *link = block_link(block);
    link->next = free_list.next;
    link->previous = &free_list;
    free_list.next->previous = link;
    free_list.next = link;
}

static int arena_create(size_t min_block_size)
{
    size_t size = ARENA_OVERHEAD + min_block_size;
    if (size < DEFAULT_ARENA_SIZE) {
        size = DEFAULT_ARENA_SIZE;
    }
    size = (size + PAGE_SIZE - 1) & ~(PAGE_SIZE - 1);

    struct arena *arena = mmap_pages(size);
    if (arena == NULL) {
        return 0;
    }
    arena->size = size;
    arena_count++;

    struct block *first = (struct block *)((char *)arena + sizeof(struct arena));
    first->previous_size = 0;
    make_free_block(first, size - ARENA_OVERHEAD);
    /* End marker: looks like a used block, so merging stops here. */
    next_block(first)->size_and_flags = BLOCK_USED;
    return 1;
}

static struct block *find_free_block(size_t size)
{
    for (struct free_link *link = free_list.next; link != &free_list; link = link->next) {
        if (block_size(block_from_link(link)) >= size) {
            return block_from_link(link);
        }
    }
    return NULL;
}

void *malloc(size_t size)
{
    if (size == 0 || size > SIZE_MAX / 2) {
        return NULL;
    }
    size_t needed = sizeof(struct block) + ((size + ALIGNMENT - 1) & ~(size_t)(ALIGNMENT - 1));
    if (needed < MIN_BLOCK_SIZE) {
        needed = MIN_BLOCK_SIZE;
    }

    struct block *block = find_free_block(needed);
    if (block == NULL) {
        if (!arena_create(needed)) {
            return NULL;
        }
        block = find_free_block(needed);
    }

    size_t available = block_size(block);
    list_remove(block_link(block));
    if (available - needed >= MIN_BLOCK_SIZE) {
        block->size_and_flags = needed;
        make_free_block(next_block(block), available - needed);
        next_block(block)->previous_size = needed;
    } else {
        needed = available;
    }
    block->size_and_flags = needed | BLOCK_USED;
    bytes_in_use += needed;
    return (char *)block + sizeof(struct block);
}

void free(void *pointer)
{
    if (pointer == NULL) {
        return;
    }
    struct block *block = (struct block *)((char *)pointer - sizeof(struct block));
    if (!block_is_used(block)) {
        abort(); /* double free or a pointer that never came from malloc */
    }
    size_t size = block_size(block);
    bytes_in_use -= size;

    struct block *next = next_block(block);
    if (!block_is_used(next)) {
        list_remove(block_link(next));
        size += block_size(next);
    }
    if (block->previous_size != 0) {
        struct block *previous = (struct block *)((char *)block - block->previous_size);
        if (!block_is_used(previous)) {
            list_remove(block_link(previous));
            size += block_size(previous);
            block = previous;
        }
    }
    make_free_block(block, size);

    /* An arena that is entirely free goes back to the kernel, except the
     * last one: keeping it avoids a system call per malloc/free pair. */
    if (block->previous_size == 0 && block_size(next_block(block)) == 0 && arena_count > 1) {
        struct arena *arena = (struct arena *)((char *)block - sizeof(struct arena));
        list_remove(block_link(block));
        arena_count--;
        munmap_pages(arena, arena->size);
    }
}

void *calloc(size_t count, size_t size)
{
    if (size != 0 && count > SIZE_MAX / size) {
        return NULL; /* count * size would overflow */
    }
    void *pointer = malloc(count * size);
    if (pointer != NULL) {
        memset(pointer, 0, count * size);
    }
    return pointer;
}

void *realloc(void *pointer, size_t size)
{
    if (pointer == NULL) {
        return malloc(size);
    }
    if (size == 0) {
        free(pointer);
        return NULL;
    }
    struct block *block = (struct block *)((char *)pointer - sizeof(struct block));
    size_t old_size = block_size(block) - sizeof(struct block);
    if (size <= old_size) {
        return pointer;
    }
    void *replacement = malloc(size);
    if (replacement != NULL) {
        memcpy(replacement, pointer, old_size);
        free(pointer);
    }
    return replacement;
}
