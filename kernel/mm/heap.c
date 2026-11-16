/*
 * First-fit free-list allocator with boundary tags.
 *
 * The heap gets memory from the vmm in "arenas" of whole pages. An arena is
 * a sequence of blocks that exactly tile it:
 *
 *   [arena header][block][block]...[block][end marker]
 *
 * Every block starts with a header holding its own size and the size of the
 * block before it. With those two numbers both neighbours of a block can be
 * found by pointer arithmetic, so a freed block can be merged with free
 * neighbours in constant time. Merging is what keeps the heap from
 * fragmenting into pieces too small to use.
 *
 * Free blocks are additionally linked into one list; the link is stored in
 * the block's unused payload, so the list costs no memory.
 *
 * State (all protected by `lock`): free_blocks, arenas, initial_arena, stats.
 * Lock order: heap lock, then the vmm lock.
 */
#include <mm/heap.h>

#include <stdbool.h>
#include <stdint.h>

#include <core/list.h>
#include <core/panic.h>
#include <core/spinlock.h>
#include <core/string.h>
#include <mm/vmm.h>

#define ALIGNMENT 16
#define DEFAULT_ARENA_PAGES 16

/* Block sizes are multiples of 16, so the low bit is free to hold a flag. */
#define BLOCK_USED 1UL

struct block {
    size_t size_and_flags; /* whole block, header included */
    size_t previous_size;  /* 0 for the first block of an arena */
};

struct arena {
    struct list_node link;
    size_t pages;
    size_t padding; /* keeps the header a multiple of ALIGNMENT */
};

/* A free block must be able to hold its list link. */
#define MIN_BLOCK_SIZE (sizeof(struct block) + sizeof(struct list_node))
/* Bytes of an arena that are not available to blocks: the arena header and
 * the end marker (a block header with size 0). */
#define ARENA_OVERHEAD (sizeof(struct arena) + sizeof(struct block))

_Static_assert(sizeof(struct block) % ALIGNMENT == 0, "block header must preserve alignment");
_Static_assert(sizeof(struct arena) % ALIGNMENT == 0, "arena header must preserve alignment");

static struct spinlock lock = SPINLOCK_INIT;
static struct list_node free_blocks;
static struct list_node arenas;
static struct arena *initial_arena;
static struct heap_stats stats;

static size_t block_size(const struct block *block)
{
    return block->size_and_flags & ~BLOCK_USED;
}

static bool block_is_used(const struct block *block)
{
    return (block->size_and_flags & BLOCK_USED) != 0;
}

static struct block *next_block(struct block *block)
{
    return (struct block *)((char *)block + block_size(block));
}

static struct block *previous_block(struct block *block)
{
    return (struct block *)((char *)block - block->previous_size);
}

static void *block_payload(struct block *block)
{
    return (char *)block + sizeof(struct block);
}

static struct list_node *block_link(struct block *block)
{
    return block_payload(block);
}

static struct block *block_from_link(struct list_node *link)
{
    return (struct block *)((char *)link - sizeof(struct block));
}

/* Turns [block, block + size) into one free block and lists it. */
static void make_free_block(struct block *block, size_t size)
{
    block->size_and_flags = size;
    next_block(block)->previous_size = size;
    list_push_front(&free_blocks, block_link(block));
}

/* Creates an arena whose single free block is at least `min_block_size`. */
static struct arena *arena_create(size_t min_block_size)
{
    size_t needed = ARENA_OVERHEAD + min_block_size;
    size_t pages = (needed + ARCH_PAGE_SIZE - 1) / ARCH_PAGE_SIZE;
    if (pages < DEFAULT_ARENA_PAGES) {
        pages = DEFAULT_ARENA_PAGES;
    }

    struct arena *arena = vmm_alloc_pages(pages, ARCH_MAP_WRITE);
    if (arena == NULL) {
        return NULL;
    }
    arena->pages = pages;
    list_push_back(&arenas, &arena->link);
    stats.arena_bytes += pages * ARCH_PAGE_SIZE;
    stats.arena_count++;

    struct block *first = (struct block *)((char *)arena + sizeof(struct arena));
    size_t size = pages * ARCH_PAGE_SIZE - ARENA_OVERHEAD;
    first->previous_size = 0;
    make_free_block(first, size);

    /* The end marker looks like a used block, so merging never runs off the
     * end of the arena. */
    next_block(first)->size_and_flags = BLOCK_USED;
    return arena;
}

static void arena_destroy(struct arena *arena)
{
    list_remove(&arena->link);
    stats.arena_bytes -= arena->pages * ARCH_PAGE_SIZE;
    stats.arena_count--;
    vmm_free_pages(arena, arena->pages);
}

static struct block *find_free_block(size_t size)
{
    list_for_each (link, &free_blocks) {
        struct block *block = block_from_link(link);
        if (block_size(block) >= size) {
            return block;
        }
    }
    return NULL;
}

/* Takes `size` bytes from the start of a free block; the rest stays free if
 * it is large enough to be a block of its own. */
static void allocate_from(struct block *block, size_t size)
{
    size_t available = block_size(block);
    list_remove(block_link(block));

    if (available - size >= MIN_BLOCK_SIZE) {
        block->size_and_flags = size;
        make_free_block(next_block(block), available - size);
        next_block(block)->previous_size = size;
    } else {
        size = available;
    }
    block->size_and_flags = size | BLOCK_USED;
    stats.used_bytes += size;
}

static size_t block_size_for(size_t payload_size)
{
    size_t size = sizeof(struct block) + ((payload_size + ALIGNMENT - 1) & ~(size_t)(ALIGNMENT - 1));
    return size < MIN_BLOCK_SIZE ? MIN_BLOCK_SIZE : size;
}

void heap_init(void)
{
    list_init(&free_blocks);
    list_init(&arenas);
    initial_arena = arena_create(0);
    if (initial_arena == NULL) {
        panic("heap: out of memory creating the first arena");
    }
}

void *kmalloc(size_t size)
{
    /* Sizes near SIZE_MAX would overflow the rounding below. */
    if (size == 0 || size > SIZE_MAX / 2) {
        return NULL;
    }
    size_t needed = block_size_for(size);

    arch_irq_state_t irq_state = spinlock_acquire(&lock);

    void *result = NULL;
    struct block *block = find_free_block(needed);
    if (block == NULL && arena_create(needed) != NULL) {
        block = find_free_block(needed);
    }
    if (block != NULL) {
        allocate_from(block, needed);
        result = block_payload(block);
    }

    spinlock_release(&lock, irq_state);
    return result;
}

void *kzalloc(size_t size)
{
    void *pointer = kmalloc(size);
    if (pointer != NULL) {
        memset(pointer, 0, size);
    }
    return pointer;
}

/* An arena whose only block is free again can go back to the vmm. The first
 * arena is kept so that a lone alloc/free pair does not map and unmap pages
 * every time. */
static void release_arena_if_empty(struct block *block)
{
    if (block->previous_size != 0 || block_size(next_block(block)) != 0) {
        return;
    }
    struct arena *arena = (struct arena *)((char *)block - sizeof(struct arena));
    if (arena != initial_arena) {
        list_remove(block_link(block));
        arena_destroy(arena);
    }
}

void kfree(void *pointer)
{
    if (pointer == NULL) {
        return;
    }
    struct block *block = (struct block *)((char *)pointer - sizeof(struct block));

    arch_irq_state_t irq_state = spinlock_acquire(&lock);

    if (((uintptr_t)pointer % ALIGNMENT) != 0 || !block_is_used(block) || block_size(block) < MIN_BLOCK_SIZE) {
        panic("heap: invalid or double free of %p", pointer);
    }
    size_t size = block_size(block);
    stats.used_bytes -= size;

    /* Merge with the following block, then with the preceding one. */
    struct block *next = next_block(block);
    if (!block_is_used(next)) {
        list_remove(block_link(next));
        size += block_size(next);
    }
    if (block->previous_size != 0 && !block_is_used(previous_block(block))) {
        block = previous_block(block);
        list_remove(block_link(block));
        size += block_size(block);
    }
    make_free_block(block, size);
    release_arena_if_empty(block);

    spinlock_release(&lock, irq_state);
}

struct heap_stats heap_stats(void)
{
    arch_irq_state_t irq_state = spinlock_acquire(&lock);
    struct heap_stats copy = stats;
    spinlock_release(&lock, irq_state);
    return copy;
}
