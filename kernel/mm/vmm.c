/*
 * State (protected by `lock`):
 *   the kernel page tables  (arch_mmu_* does not lock by itself)
 *   ranges                  one bit per page of the kernel dynamic area,
 *                           set = virtual address in use
 *   range_hint              where the next search starts
 * dynamic_base is written once in vmm_init().
 *
 * Lock order: vmm lock, then the pmm lock (taken inside pmm_alloc/pmm_free).
 */
#include <mm/vmm.h>

#include <core/bitmap.h>
#include <core/log.h>
#include <core/panic.h>
#include <core/spinlock.h>
#include <core/string.h>
#include <mm/pmm.h>

static struct spinlock lock = SPINLOCK_INIT;
static struct bitmap ranges;
static size_t range_hint;
static uintptr_t dynamic_base;

static const struct arch_mmu_frame_ops frame_ops = {
    .alloc = pmm_alloc,
    .free = pmm_free,
};

static void init_range_bitmap(void)
{
    size_t page_count = arch_mmu_kernel_dynamic_size() / ARCH_PAGE_SIZE;
    size_t bytes = bitmap_storage_size(page_count);
    size_t frame_count = (bytes + ARCH_PAGE_SIZE - 1) / ARCH_PAGE_SIZE;

    /* Contiguous frames are contiguous in the direct map too, so the bitmap
     * can live there without any mapping work. */
    phys_addr_t storage = pmm_alloc_contiguous(frame_count);
    if (storage == 0) {
        panic("vmm: out of memory for the virtual range bitmap");
    }
    uint64_t *words = arch_phys_to_virt(storage);
    memset(words, 0, frame_count * ARCH_PAGE_SIZE);
    bitmap_init(&ranges, words, page_count);
}

void vmm_init(void)
{
    const char *problem = arch_mmu_init(&frame_ops);
    if (problem != NULL) {
        panic("vmm: %s", problem);
    }
    dynamic_base = arch_mmu_kernel_dynamic_base();
    init_range_bitmap();

    log_info("vmm: kernel page tables active, dynamic area %p + %zu MiB", (void *)dynamic_base,
             arch_mmu_kernel_dynamic_size() >> 20);
}

static uintptr_t page_address(size_t index)
{
    return dynamic_base + index * ARCH_PAGE_SIZE;
}

/* Unmaps `count` pages starting at page `first` and frees their frames. */
static void unmap_pages_locked(size_t first, size_t count)
{
    for (size_t i = 0; i < count; i++) {
        phys_addr_t frame;
        if (!arch_mmu_unmap(arch_mmu_kernel_space(), page_address(first + i), &frame)) {
            panic("vmm: page %p is not mapped", (void *)page_address(first + i));
        }
        pmm_free(frame);
    }
}

/* Backs `count` pages starting at page `first` with zeroed frames. On
 * failure nothing stays mapped. */
static bool map_pages_locked(size_t first, size_t count, unsigned flags)
{
    for (size_t i = 0; i < count; i++) {
        phys_addr_t frame = pmm_alloc();
        if (frame == 0) {
            unmap_pages_locked(first, i);
            return false;
        }
        /* Zero through the direct map, before the page becomes reachable at
         * its new address: old contents must never leak to the new owner. */
        memset(arch_phys_to_virt(frame), 0, ARCH_PAGE_SIZE);

        if (!arch_mmu_map(arch_mmu_kernel_space(), page_address(first + i), frame, flags)) {
            pmm_free(frame);
            unmap_pages_locked(first, i);
            return false;
        }
    }
    return true;
}

/*
 * Reserves `reserved` consecutive virtual pages and maps the last `mapped`
 * of them; the leading (reserved - mapped) pages stay unmapped guard pages.
 * Returns the index of the first reserved page or BITMAP_NOT_FOUND.
 */
static size_t alloc_range(size_t reserved, size_t mapped, unsigned flags)
{
    arch_irq_state_t irq_state = spinlock_acquire(&lock);

    size_t first = bitmap_find_clear_run(&ranges, range_hint, reserved);
    if (first != BITMAP_NOT_FOUND) {
        if (map_pages_locked(first + reserved - mapped, mapped, flags)) {
            bitmap_set_range(&ranges, first, reserved);
            range_hint = first + reserved;
        } else {
            first = BITMAP_NOT_FOUND;
        }
    }

    spinlock_release(&lock, irq_state);
    return first;
}

static void free_range(size_t first, size_t reserved, size_t mapped)
{
    arch_irq_state_t irq_state = spinlock_acquire(&lock);

    if (first + reserved > ranges.bit_count || !bitmap_all_set(&ranges, first, reserved)) {
        panic("vmm: freeing %zu page(s) at %p that are not allocated", reserved, (void *)page_address(first));
    }
    unmap_pages_locked(first + reserved - mapped, mapped);
    bitmap_clear_range(&ranges, first, reserved);
    if (first < range_hint) {
        range_hint = first;
    }

    spinlock_release(&lock, irq_state);
}

static size_t page_index(uintptr_t address)
{
    return (address - dynamic_base) / ARCH_PAGE_SIZE;
}

void *vmm_alloc_pages(size_t count, unsigned flags)
{
    if (count == 0) {
        return NULL;
    }
    size_t first = alloc_range(count, count, flags);
    return first == BITMAP_NOT_FOUND ? NULL : (void *)page_address(first);
}

void vmm_free_pages(void *base, size_t count)
{
    free_range(page_index((uintptr_t)base), count, count);
}

void *vmm_alloc_kernel_stack(size_t pages)
{
    if (pages == 0) {
        return NULL;
    }
    /* One extra page of address space below the stack, never mapped. */
    size_t first = alloc_range(pages + 1, pages, ARCH_MAP_WRITE);
    return first == BITMAP_NOT_FOUND ? NULL : (void *)page_address(first + pages + 1);
}

void vmm_free_kernel_stack(void *stack_top, size_t pages)
{
    free_range(page_index((uintptr_t)stack_top) - (pages + 1), pages + 1, pages);
}

/* Number of pages needed to cover `size` bytes starting `offset` bytes into
 * the first page. */
static size_t pages_spanned(size_t offset, size_t size)
{
    return (offset + size + ARCH_PAGE_SIZE - 1) / ARCH_PAGE_SIZE;
}

void *vmm_map_physical(phys_addr_t address, size_t size)
{
    if (size == 0) {
        return NULL;
    }
    size_t offset = address % ARCH_PAGE_SIZE;
    phys_addr_t first_frame = address - offset;
    size_t count = pages_spanned(offset, size);

    arch_irq_state_t irq_state = spinlock_acquire(&lock);

    void *result = NULL;
    size_t first = bitmap_find_clear_run(&ranges, range_hint, count);
    if (first != BITMAP_NOT_FOUND) {
        size_t mapped = 0;
        while (mapped < count
               && arch_mmu_map(arch_mmu_kernel_space(), page_address(first + mapped),
                               first_frame + mapped * ARCH_PAGE_SIZE, ARCH_MAP_WRITE)) {
            mapped++;
        }
        if (mapped == count) {
            bitmap_set_range(&ranges, first, count);
            range_hint = first + count;
            result = (void *)(page_address(first) + offset);
        } else {
            /* Out of memory for page tables: undo the partial mapping. */
            for (size_t i = 0; i < mapped; i++) {
                arch_mmu_unmap(arch_mmu_kernel_space(), page_address(first + i), NULL);
            }
        }
    }

    spinlock_release(&lock, irq_state);
    return result;
}

void vmm_unmap_physical(void *pointer, size_t size)
{
    size_t offset = (uintptr_t)pointer % ARCH_PAGE_SIZE;
    size_t first = page_index((uintptr_t)pointer - offset);
    size_t count = pages_spanned(offset, size);

    arch_irq_state_t irq_state = spinlock_acquire(&lock);

    if (first + count > ranges.bit_count || !bitmap_all_set(&ranges, first, count)) {
        panic("vmm: unmapping %zu page(s) at %p that are not mapped", count, pointer);
    }
    for (size_t i = 0; i < count; i++) {
        /* The frames are not ours to free: they belong to a device or to
         * whoever allocated them. */
        arch_mmu_unmap(arch_mmu_kernel_space(), page_address(first + i), NULL);
    }
    bitmap_clear_range(&ranges, first, count);
    if (first < range_hint) {
        range_hint = first;
    }

    spinlock_release(&lock, irq_state);
}

bool vmm_query(const void *address, phys_addr_t *phys, unsigned *flags)
{
    arch_irq_state_t irq_state = spinlock_acquire(&lock);
    bool mapped = arch_mmu_translate(arch_mmu_kernel_space(), (uintptr_t)address, phys, flags);
    spinlock_release(&lock, irq_state);
    return mapped;
}
