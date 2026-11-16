/*
 * Bitmap frame allocator: one bit per physical frame, set = in use.
 *
 * A bitmap is slower than a free list for single frames but can find runs
 * of consecutive frames, which device drivers and large buffers need.
 *
 * State (all protected by `lock`):
 *   frames       the bitmap; its storage is carved out of usable RAM in
 *                pmm_init() and reached through the direct map
 *   free_frames  number of clear bits
 *   search_hint  where the next search starts
 * total_frames is written once in pmm_init().
 */
#include <mm/pmm.h>

#include <arch/boot.h>
#include <core/bitmap.h>
#include <core/log.h>
#include <core/panic.h>
#include <core/spinlock.h>

static struct spinlock lock = SPINLOCK_INIT;
static struct bitmap frames;
static size_t total_frames;
static size_t free_frames;
static size_t search_hint;

static const char *memory_type_name(enum arch_memory_type type)
{
    switch (type) {
    case ARCH_MEMORY_USABLE:
        return "usable";
    case ARCH_MEMORY_RESERVED:
        return "reserved";
    case ARCH_MEMORY_ACPI_RECLAIMABLE:
        return "ACPI reclaimable";
    case ARCH_MEMORY_ACPI_NVS:
        return "ACPI NVS";
    case ARCH_MEMORY_BAD:
        return "bad";
    case ARCH_MEMORY_BOOT_RECLAIMABLE:
        return "boot reclaimable";
    case ARCH_MEMORY_KERNEL_IMAGE:
        return "kernel image";
    case ARCH_MEMORY_FRAMEBUFFER:
        return "framebuffer";
    }
    return "unknown";
}

static void log_memory_map(void)
{
    size_t count = arch_boot_memory_region_count();
    for (size_t i = 0; i < count; i++) {
        struct arch_memory_region region = arch_boot_memory_region(i);
        log_info("pmm:   %016llx-%016llx %s", (unsigned long long)region.base,
                 (unsigned long long)(region.base + region.length), memory_type_name(region.type));
    }
}

/* The bitmap must cover every usable frame, so it ends where usable RAM ends. */
static size_t count_frames_to_track(void)
{
    uint64_t highest_end = 0;
    size_t count = arch_boot_memory_region_count();

    for (size_t i = 0; i < count; i++) {
        struct arch_memory_region region = arch_boot_memory_region(i);
        if (region.type == ARCH_MEMORY_USABLE && region.base + region.length > highest_end) {
            highest_end = region.base + region.length;
        }
    }
    return highest_end / ARCH_PAGE_SIZE;
}

/*
 * The allocator needs memory for its bitmap before it can allocate memory.
 * Break the cycle by taking it straight from the first usable region that is
 * large enough. Returns 0 if there is none.
 */
static phys_addr_t find_bitmap_home(size_t bytes)
{
    size_t count = arch_boot_memory_region_count();

    for (size_t i = 0; i < count; i++) {
        struct arch_memory_region region = arch_boot_memory_region(i);
        if (region.type != ARCH_MEMORY_USABLE) {
            continue;
        }
        /* Never frame 0: address 0 is our "no frame" value. */
        uint64_t base = region.base == 0 ? ARCH_PAGE_SIZE : region.base;
        if (base + bytes <= region.base + region.length) {
            return base;
        }
    }
    return 0;
}

static void mark_usable_regions_free(void)
{
    size_t count = arch_boot_memory_region_count();

    for (size_t i = 0; i < count; i++) {
        struct arch_memory_region region = arch_boot_memory_region(i);
        if (region.type == ARCH_MEMORY_USABLE) {
            bitmap_clear_range(&frames, region.base / ARCH_PAGE_SIZE, region.length / ARCH_PAGE_SIZE);
        }
    }
}

void pmm_init(void)
{
    log_info("pmm: physical memory map:");
    log_memory_map();

    size_t frame_count = count_frames_to_track();
    size_t bitmap_bytes = bitmap_storage_size(frame_count);
    size_t bitmap_frames = (bitmap_bytes + ARCH_PAGE_SIZE - 1) / ARCH_PAGE_SIZE;

    phys_addr_t bitmap_home = find_bitmap_home(bitmap_frames * ARCH_PAGE_SIZE);
    if (bitmap_home == 0) {
        panic("pmm: no usable region can hold the %zu byte frame bitmap", bitmap_bytes);
    }
    bitmap_init(&frames, arch_phys_to_virt(bitmap_home), frame_count);

    /* Start from "everything is in use" and free only what the memory map
     * says is usable: holes and reserved ranges then stay unavailable
     * without any special handling. */
    bitmap_set_range(&frames, 0, frame_count);
    mark_usable_regions_free();
    total_frames = bitmap_count_clear(&frames);

    bitmap_set_range(&frames, bitmap_home / ARCH_PAGE_SIZE, bitmap_frames);
    bitmap_set_range(&frames, 0, 1);
    free_frames = bitmap_count_clear(&frames);

    log_info("pmm: %zu MiB usable, %zu frames free, bitmap of %zu bytes at %p",
             total_frames * ARCH_PAGE_SIZE >> 20, free_frames, bitmap_bytes, (void *)bitmap_home);
}

phys_addr_t pmm_alloc_contiguous(size_t count)
{
    arch_irq_state_t irq_state = spinlock_acquire(&lock);

    phys_addr_t result = 0;
    size_t first = bitmap_find_clear_run(&frames, search_hint, count);
    if (first != BITMAP_NOT_FOUND) {
        bitmap_set_range(&frames, first, count);
        free_frames -= count;
        search_hint = first + count;
        result = (phys_addr_t)first * ARCH_PAGE_SIZE;
    }

    spinlock_release(&lock, irq_state);
    return result;
}

phys_addr_t pmm_alloc(void)
{
    return pmm_alloc_contiguous(1);
}

void pmm_free_contiguous(phys_addr_t first_frame, size_t count)
{
    size_t first = first_frame / ARCH_PAGE_SIZE;

    arch_irq_state_t irq_state = spinlock_acquire(&lock);

    bool valid = (first_frame % ARCH_PAGE_SIZE) == 0 && count > 0 && first + count <= frames.bit_count
              && bitmap_all_set(&frames, first, count);
    if (valid) {
        bitmap_clear_range(&frames, first, count);
        free_frames += count;
        /* Prefer reusing low frames: keeps allocations compact. */
        if (first < search_hint) {
            search_hint = first;
        }
    }

    spinlock_release(&lock, irq_state);

    if (!valid) {
        /* Usually a double free. Continuing would hand one frame to two
         * owners later, which corrupts memory far from the actual bug. */
        panic("pmm: invalid free of %zu frame(s) at %p", count, (void *)first_frame);
    }
}

void pmm_free(phys_addr_t frame)
{
    pmm_free_contiguous(frame, 1);
}

struct pmm_stats pmm_stats(void)
{
    arch_irq_state_t irq_state = spinlock_acquire(&lock);
    struct pmm_stats stats = { .total_frames = total_frames, .free_frames = free_frames };
    spinlock_release(&lock, irq_state);
    return stats;
}
