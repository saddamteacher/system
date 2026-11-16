/* Tests for kernel/mm. */
#include "tests_internal.h"

#include <stddef.h>

#include <core/log.h>
#include <core/string.h>
#include <mm/heap.h>
#include <mm/pmm.h>
#include <mm/vmm.h>

/* Logs the failed condition and makes the enclosing test return false. */
#define EXPECT(condition) \
    do { \
        if (!(condition)) { \
            log_error("%s:%d: expectation failed: %s", __FILE__, __LINE__, #condition); \
            return false; \
        } \
    } while (0)

#define PMM_TEST_FRAMES 64

bool test_pmm(void)
{
    static phys_addr_t frames[PMM_TEST_FRAMES];
    size_t free_before = pmm_stats().free_frames;

    for (size_t i = 0; i < PMM_TEST_FRAMES; i++) {
        frames[i] = pmm_alloc();
        EXPECT(frames[i] != 0);
        EXPECT(frames[i] % ARCH_PAGE_SIZE == 0);
        /* Tag each frame with its own index... */
        *(size_t *)arch_phys_to_virt(frames[i]) = i;
    }
    EXPECT(pmm_stats().free_frames == free_before - PMM_TEST_FRAMES);

    /* ...if two allocations returned the same frame, a tag was overwritten. */
    for (size_t i = 0; i < PMM_TEST_FRAMES; i++) {
        EXPECT(*(size_t *)arch_phys_to_virt(frames[i]) == i);
    }

    /* A freed frame becomes available again. */
    pmm_free(frames[0]);
    EXPECT(pmm_stats().free_frames == free_before - PMM_TEST_FRAMES + 1);
    frames[0] = pmm_alloc();
    EXPECT(frames[0] != 0);

    for (size_t i = 0; i < PMM_TEST_FRAMES; i++) {
        pmm_free(frames[i]);
    }
    EXPECT(pmm_stats().free_frames == free_before);

    phys_addr_t run = pmm_alloc_contiguous(8);
    EXPECT(run != 0 && run % ARCH_PAGE_SIZE == 0);
    EXPECT(pmm_stats().free_frames == free_before - 8);
    /* Contiguous in physical memory means contiguous in the direct map. */
    memset(arch_phys_to_virt(run), 0xAB, 8 * ARCH_PAGE_SIZE);
    pmm_free_contiguous(run, 8);
    EXPECT(pmm_stats().free_frames == free_before);

    /* More frames than exist must fail cleanly, not hang or corrupt. */
    EXPECT(pmm_alloc_contiguous(pmm_stats().total_frames + 1) == 0);
    EXPECT(pmm_stats().free_frames == free_before);
    return true;
}

#define VMM_TEST_PAGES 3

static bool kernel_image_is_protected(void)
{
    static const int constant = 1;
    static int variable = 1;
    unsigned flags;

    /* Code: executable, not writable. */
    EXPECT(vmm_query((const void *)(uintptr_t)kernel_image_is_protected, NULL, &flags));
    EXPECT((flags & ARCH_MAP_EXEC) && !(flags & ARCH_MAP_WRITE));
    /* Constants: neither. */
    EXPECT(vmm_query(&constant, NULL, &flags));
    EXPECT(!(flags & ARCH_MAP_EXEC) && !(flags & ARCH_MAP_WRITE));
    /* Data: writable, not executable. */
    EXPECT(vmm_query(&variable, NULL, &flags));
    EXPECT((flags & ARCH_MAP_WRITE) && !(flags & ARCH_MAP_EXEC));
    return true;
}

bool test_vmm(void)
{
    EXPECT(kernel_image_is_protected());

    /* The first allocation may create page tables that stay allocated, so
     * warm up once before measuring. */
    void *warmup = vmm_alloc_pages(VMM_TEST_PAGES, ARCH_MAP_WRITE);
    EXPECT(warmup != NULL);
    vmm_free_pages(warmup, VMM_TEST_PAGES);
    size_t free_before = pmm_stats().free_frames;

    unsigned char *pages = vmm_alloc_pages(VMM_TEST_PAGES, ARCH_MAP_WRITE);
    EXPECT(pages != NULL);
    EXPECT((uintptr_t)pages % ARCH_PAGE_SIZE == 0);
    EXPECT(pmm_stats().free_frames == free_before - VMM_TEST_PAGES);

    for (size_t i = 0; i < VMM_TEST_PAGES; i++) {
        unsigned char *page = pages + i * ARCH_PAGE_SIZE;
        phys_addr_t frame;
        unsigned flags;

        EXPECT(page[0] == 0 && page[ARCH_PAGE_SIZE - 1] == 0); /* zero-filled */
        EXPECT(vmm_query(page, &frame, &flags));
        EXPECT((flags & ARCH_MAP_WRITE) && !(flags & ARCH_MAP_EXEC) && !(flags & ARCH_MAP_USER));

        /* The same frame seen through two addresses: a write through the new
         * mapping must be visible through the direct map. */
        page[5] = (unsigned char)(0xC0 + i);
        EXPECT(*((unsigned char *)arch_phys_to_virt(frame) + 5) == 0xC0 + i);
    }

    vmm_free_pages(pages, VMM_TEST_PAGES);
    EXPECT(!vmm_query(pages, NULL, NULL));
    EXPECT(pmm_stats().free_frames == free_before);

    /* Mapping existing physical memory: the frame keeps its owner. */
    phys_addr_t frame = pmm_alloc();
    EXPECT(frame != 0);
    memcpy((char *)arch_phys_to_virt(frame) + 0x10, "device", 7);
    char *window = vmm_map_physical(frame + 0x10, 32);
    EXPECT(window != NULL);
    EXPECT((uintptr_t)window % ARCH_PAGE_SIZE == 0x10); /* offset preserved */
    EXPECT(strcmp(window, "device") == 0);
    vmm_unmap_physical(window, 32);
    EXPECT(!vmm_query(window, NULL, NULL));
    EXPECT(pmm_stats().free_frames == free_before - 1); /* not freed by unmap */
    pmm_free(frame);

    /* A stack: mapped below its top, guard page below that. */
    unsigned char *stack_top = vmm_alloc_kernel_stack(2);
    EXPECT(stack_top != NULL);
    EXPECT(vmm_query(stack_top - 1, NULL, NULL));
    EXPECT(vmm_query(stack_top - 2 * ARCH_PAGE_SIZE, NULL, NULL));
    EXPECT(!vmm_query(stack_top - 2 * ARCH_PAGE_SIZE - 1, NULL, NULL));
    vmm_free_kernel_stack(stack_top, 2);
    EXPECT(pmm_stats().free_frames == free_before);
    return true;
}

static bool heap_basics(void)
{
    EXPECT(kmalloc(0) == NULL);

    char *a = kmalloc(1);
    char *b = kmalloc(100);
    EXPECT(a != NULL && b != NULL && a != b);
    EXPECT((uintptr_t)a % 16 == 0 && (uintptr_t)b % 16 == 0);
    kfree(a);
    kfree(b);
    kfree(NULL);

    unsigned char *zeroed = kzalloc(300);
    EXPECT(zeroed != NULL);
    for (size_t i = 0; i < 300; i++) {
        EXPECT(zeroed[i] == 0);
    }
    kfree(zeroed);
    return true;
}

static bool heap_merges_free_neighbours(void)
{
    /* Three adjacent blocks, freed in an order that exercises merging with
     * the next block (b after c) and with both sides (b between a and c). */
    char *a = kmalloc(200);
    char *b = kmalloc(200);
    char *c = kmalloc(200);
    EXPECT(a != NULL && b != NULL && c != NULL);

    kfree(a);
    kfree(c);
    kfree(b);

    /* If the three were merged back into one block, an allocation as large
     * as all of them fits exactly where `a` was. */
    char *merged = kmalloc(600);
    EXPECT(merged == a);
    kfree(merged);
    return true;
}

static bool heap_large_allocation(void)
{
    struct heap_stats before = heap_stats();

    /* Larger than a default arena: needs an arena of its own... */
    size_t size = 1024 * 1024;
    unsigned char *large = kmalloc(size);
    EXPECT(large != NULL);
    EXPECT(heap_stats().arena_count == before.arena_count + 1);
    large[0] = 1;
    large[size - 1] = 2;

    /* ...which goes back to the vmm when it is freed. */
    kfree(large);
    EXPECT(heap_stats().arena_count == before.arena_count);
    EXPECT(heap_stats().arena_bytes == before.arena_bytes);
    return true;
}

#define STRESS_SLOTS 64
#define STRESS_ROUNDS 4000

/* Deterministic pseudo-random numbers, so a failure is reproducible. */
static uint32_t stress_random(uint32_t *state)
{
    *state = *state * 1664525u + 1013904223u;
    return *state >> 8;
}

static bool block_holds(const unsigned char *block, size_t size, unsigned char value)
{
    for (size_t i = 0; i < size; i++) {
        if (block[i] != value) {
            return false;
        }
    }
    return true;
}

static bool heap_stress(void)
{
    static unsigned char *slots[STRESS_SLOTS];
    static size_t sizes[STRESS_SLOTS];
    uint32_t random = 12345;

    for (int round = 0; round < STRESS_ROUNDS; round++) {
        size_t slot = stress_random(&random) % STRESS_SLOTS;

        if (slots[slot] != NULL) {
            /* Each block is filled with its slot number. If the allocator
             * ever handed out overlapping blocks, some fill was overwritten. */
            EXPECT(block_holds(slots[slot], sizes[slot], (unsigned char)slot));
            kfree(slots[slot]);
            slots[slot] = NULL;
            continue;
        }
        /* Mostly small blocks, occasionally one bigger than an arena. */
        size_t size = 1 + stress_random(&random) % 2048;
        if (stress_random(&random) % 64 == 0) {
            size = 100 * 1024;
        }
        slots[slot] = kmalloc(size);
        EXPECT(slots[slot] != NULL);
        sizes[slot] = size;
        memset(slots[slot], (int)slot, size);
    }

    for (size_t slot = 0; slot < STRESS_SLOTS; slot++) {
        if (slots[slot] != NULL) {
            EXPECT(block_holds(slots[slot], sizes[slot], (unsigned char)slot));
            kfree(slots[slot]);
            slots[slot] = NULL;
        }
    }
    return true;
}

bool test_heap(void)
{
    /* Warm up page tables for the areas the test will touch. */
    kfree(kmalloc(1024 * 1024));
    struct heap_stats heap_before = heap_stats();
    size_t frames_before = pmm_stats().free_frames;

    EXPECT(heap_basics());
    EXPECT(heap_merges_free_neighbours());
    EXPECT(heap_large_allocation());
    EXPECT(heap_stress());

    /* Leak check: everything was freed, so the heap and the frame allocator
     * must be exactly where they started. */
    struct heap_stats heap_after = heap_stats();
    EXPECT(heap_after.used_bytes == heap_before.used_bytes);
    EXPECT(heap_after.arena_count == heap_before.arena_count);
    EXPECT(heap_after.arena_bytes == heap_before.arena_bytes);
    EXPECT(pmm_stats().free_frames == frames_before);
    return true;
}
