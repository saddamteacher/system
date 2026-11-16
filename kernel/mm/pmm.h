#ifndef MM_PMM_H
#define MM_PMM_H

#include <stddef.h>

#include <arch/mmu.h>

/*
 * Physical memory manager: hands out physical frames (ARCH_PAGE_SIZE bytes
 * of RAM each). All functions are safe to call from any context.
 *
 * Frames are returned with whatever contents they last had; callers that
 * need zeroes must clear them.
 */

/* Builds the allocator from the boot memory map. Call once. */
void pmm_init(void);

/* Returns the physical address of a free frame, or 0 if memory is exhausted.
 * Frame 0 is never handed out, so 0 is unambiguous. */
phys_addr_t pmm_alloc(void);

/* `count` physically consecutive frames; returns the first, or 0. */
phys_addr_t pmm_alloc_contiguous(size_t count);

/* Freeing a frame that is not allocated is a kernel bug and panics. */
void pmm_free(phys_addr_t frame);
void pmm_free_contiguous(phys_addr_t first_frame, size_t count);

struct pmm_stats {
    size_t total_frames; /* usable RAM found at boot */
    size_t free_frames;
};

struct pmm_stats pmm_stats(void);

#endif
