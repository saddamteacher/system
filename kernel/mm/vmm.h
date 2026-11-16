#ifndef MM_VMM_H
#define MM_VMM_H

#include <stdbool.h>
#include <stddef.h>

#include <arch/mmu.h>

/*
 * Virtual memory manager for the kernel's own address space: takes over the
 * page tables and hands out page-granular kernel virtual memory backed by
 * fresh frames. All functions are safe to call from any context.
 */

/* Switches to kernel-owned page tables. Requires pmm_init(). Call once. */
void vmm_init(void);

/*
 * Allocates `count` consecutive, zero-filled, non-executable kernel pages.
 * `flags` are ARCH_MAP_* (pass ARCH_MAP_WRITE for ordinary memory).
 * Returns NULL if virtual space or physical memory is exhausted.
 */
void *vmm_alloc_pages(size_t count, unsigned flags);

/* Unmaps the pages and returns their frames. `base` and `count` must match
 * an earlier vmm_alloc_pages() call exactly. */
void vmm_free_pages(void *base, size_t count);

/*
 * Allocates a kernel stack of `pages` pages and returns its TOP (stacks grow
 * down). The page below the stack is left unmapped as a guard: running off
 * the end faults immediately instead of overwriting unrelated memory.
 */
void *vmm_alloc_kernel_stack(size_t pages);
void vmm_free_kernel_stack(void *stack_top, size_t pages);

/* Looks up a kernel virtual address. phys and flags may be NULL. */
bool vmm_query(const void *address, phys_addr_t *phys, unsigned *flags);

#endif
