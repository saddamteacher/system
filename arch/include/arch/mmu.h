#ifndef ARCH_MMU_H
#define ARCH_MMU_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/*
 * Memory management unit: page tables and address translation.
 *
 * The arch layer knows the page table format; the portable kernel
 * (kernel/mm) decides what gets mapped where.
 */

#define ARCH_PAGE_SIZE 4096UL

/* A physical address. A distinct name keeps it from being mixed up with a
 * pointer: physical addresses cannot be dereferenced. */
typedef uint64_t phys_addr_t;

/* Mapping permissions. Every mapping is readable. */
#define ARCH_MAP_WRITE (1u << 0)
#define ARCH_MAP_EXEC  (1u << 1)
#define ARCH_MAP_USER  (1u << 2) /* accessible from user mode */

/* One set of page tables. Opaque outside the arch layer. */
struct arch_address_space;

/*
 * Page tables are themselves stored in physical frames. The arch layer gets
 * them from the kernel's frame allocator through these callbacks, so it does
 * not depend on kernel/mm. `alloc` returns 0 when out of memory.
 */
struct arch_mmu_frame_ops {
    phys_addr_t (*alloc)(void);
    void (*free)(phys_addr_t frame);
};

/*
 * Builds the kernel's own page tables and switches to them:
 *   - the kernel image, each part with the least permissions it needs,
 *   - the direct map: all RAM at arch_phys_to_virt(), writable, not executable.
 * Returns NULL on success or a description of the failure.
 */
const char *arch_mmu_init(const struct arch_mmu_frame_ops *frame_ops);

/*
 * The direct map. Valid for RAM the memory map reports as usable, boot
 * reclaimable or ACPI. Works from the first instruction of kmain(): until
 * arch_mmu_init() the bootloader's mapping provides it.
 */
void *arch_phys_to_virt(phys_addr_t address);

/* The kernel's address space. Its upper half is shared by every process. */
struct arch_address_space *arch_mmu_kernel_space(void);

/*
 * Range of kernel virtual addresses that arch_mmu_init() leaves unmapped for
 * the kernel to manage (heap, stacks, device mappings).
 */
uintptr_t arch_mmu_kernel_dynamic_base(void);
size_t arch_mmu_kernel_dynamic_size(void);

/*
 * Single-page operations. `virt` and `phys` must be page aligned.
 * These functions do not lock: the caller must serialise all changes to one
 * address space.
 *
 * map:       false if the page is already mapped or a page table could not
 *            be allocated.
 * unmap:     false if the page was not mapped; otherwise stores the frame it
 *            pointed to in *phys (if not NULL).
 * translate: works for any address, aligned or not; false if unmapped.
 *            phys and flags may be NULL.
 */
bool arch_mmu_map(struct arch_address_space *space, uintptr_t virt, phys_addr_t phys, unsigned flags);
bool arch_mmu_unmap(struct arch_address_space *space, uintptr_t virt, phys_addr_t *phys);
bool arch_mmu_translate(struct arch_address_space *space, uintptr_t virt, phys_addr_t *phys, unsigned *flags);

#endif
