/*
 * State: everything in a struct address_space is protected by its lock.
 * Lock order: aspace lock, then heap, vmm and pmm locks.
 */
#include <mm/aspace.h>

#include <core/list.h>
#include <core/spinlock.h>
#include <core/string.h>
#include <mm/heap.h>
#include <mm/pmm.h>

/* Where aspace_map_anywhere() starts handing out addresses: far above
 * where programs are loaded, far below the stack. */
#define MMAP_AREA_BASE 0x0000100000000000ULL

struct region {
    struct list_node link;
    uintptr_t start;
    size_t pages;
};

struct address_space {
    struct arch_address_space arch;
    struct spinlock lock;
    struct list_node regions;
    uintptr_t mmap_next;
};

struct address_space *aspace_create(void)
{
    struct address_space *space = kzalloc(sizeof(*space));
    if (space == NULL) {
        return NULL;
    }
    if (!arch_mmu_space_init(&space->arch)) {
        kfree(space);
        return NULL;
    }
    space->lock = (struct spinlock)SPINLOCK_INIT;
    list_init(&space->regions);
    space->mmap_next = MMAP_AREA_BASE;
    return space;
}

struct arch_address_space *aspace_arch(struct address_space *space)
{
    return &space->arch;
}

static void unmap_pages_locked(struct address_space *space, uintptr_t start, size_t pages)
{
    for (size_t i = 0; i < pages; i++) {
        phys_addr_t frame;
        if (arch_mmu_unmap(&space->arch, start + i * ARCH_PAGE_SIZE, &frame)) {
            pmm_free(frame);
        }
    }
}

static void region_destroy_locked(struct address_space *space, struct region *region)
{
    unmap_pages_locked(space, region->start, region->pages);
    list_remove(&region->link);
    kfree(region);
}

void aspace_destroy(struct address_space *space)
{
    /* Nobody else can reach the space any more, so no locking is needed. */
    while (!list_is_empty(&space->regions)) {
        region_destroy_locked(space, container_of(space->regions.next, struct region, link));
    }
    arch_mmu_space_destroy(&space->arch);
    kfree(space);
}

static bool range_in_user_space(uintptr_t start, size_t length)
{
    uintptr_t end = start + length;
    return end >= start /* no wrap-around */ && start >= arch_mmu_user_base() && end <= arch_mmu_user_end();
}

static bool overlaps_region_locked(struct address_space *space, uintptr_t start, size_t pages)
{
    uintptr_t end = start + pages * ARCH_PAGE_SIZE;

    list_for_each (node, &space->regions) {
        struct region *region = container_of(node, struct region, link);
        uintptr_t region_end = region->start + region->pages * ARCH_PAGE_SIZE;
        if (start < region_end && region->start < end) {
            return true;
        }
    }
    return false;
}

static bool map_pages_locked(struct address_space *space, uintptr_t start, size_t pages, unsigned flags)
{
    for (size_t i = 0; i < pages; i++) {
        phys_addr_t frame = pmm_alloc();
        if (frame == 0) {
            unmap_pages_locked(space, start, i);
            return false;
        }
        /* Programs must never see what a frame held before. */
        memset(arch_phys_to_virt(frame), 0, ARCH_PAGE_SIZE);
        if (!arch_mmu_map(&space->arch, start + i * ARCH_PAGE_SIZE, frame, flags | ARCH_MAP_USER)) {
            pmm_free(frame);
            unmap_pages_locked(space, start, i);
            return false;
        }
    }
    return true;
}

static bool map_locked(struct address_space *space, uintptr_t start, size_t pages, unsigned flags)
{
    if (pages == 0 || pages > SIZE_MAX / ARCH_PAGE_SIZE || (start % ARCH_PAGE_SIZE) != 0
        || !range_in_user_space(start, pages * ARCH_PAGE_SIZE) || overlaps_region_locked(space, start, pages)) {
        return false;
    }
    struct region *region = kmalloc(sizeof(*region));
    if (region == NULL) {
        return false;
    }
    if (!map_pages_locked(space, start, pages, flags)) {
        kfree(region);
        return false;
    }
    region->start = start;
    region->pages = pages;
    list_push_back(&space->regions, &region->link);
    return true;
}

bool aspace_map(struct address_space *space, uintptr_t start, size_t pages, unsigned flags)
{
    arch_irq_state_t irq_state = spinlock_acquire(&space->lock);
    bool mapped = map_locked(space, start, pages, flags);
    spinlock_release(&space->lock, irq_state);
    return mapped;
}

uintptr_t aspace_map_anywhere(struct address_space *space, size_t pages, unsigned flags)
{
    arch_irq_state_t irq_state = spinlock_acquire(&space->lock);

    uintptr_t start = space->mmap_next;
    if (map_locked(space, start, pages, flags)) {
        /* Addresses are handed out once and not reused: the range is vast,
         * and a stale pointer then faults instead of hitting a newer
         * mapping. One unmapped page separates neighbours, so running off
         * the end of a block faults too. */
        space->mmap_next = start + (pages + 1) * ARCH_PAGE_SIZE;
    } else {
        start = 0;
    }

    spinlock_release(&space->lock, irq_state);
    return start;
}

bool aspace_unmap(struct address_space *space, uintptr_t start, size_t pages)
{
    arch_irq_state_t irq_state = spinlock_acquire(&space->lock);

    bool found = false;
    list_for_each (node, &space->regions) {
        struct region *region = container_of(node, struct region, link);
        if (region->start == start && region->pages == pages) {
            region_destroy_locked(space, region);
            found = true;
            break;
        }
    }

    spinlock_release(&space->lock, irq_state);
    return found;
}

bool aspace_write(struct address_space *space, uintptr_t destination, const void *source, size_t length)
{
    const unsigned char *bytes = source;
    arch_irq_state_t irq_state = spinlock_acquire(&space->lock);

    bool ok = true;
    while (length > 0) {
        /* The space may not be active, so its addresses cannot be used
         * directly. Find the frame behind each page and write through the
         * direct map. */
        phys_addr_t frame_address;
        if (!arch_mmu_translate(&space->arch, destination, &frame_address, NULL)) {
            ok = false;
            break;
        }
        size_t in_page = ARCH_PAGE_SIZE - (destination % ARCH_PAGE_SIZE);
        size_t chunk = length < in_page ? length : in_page;
        memcpy(arch_phys_to_virt(frame_address), bytes, chunk);
        destination += chunk;
        bytes += chunk;
        length -= chunk;
    }

    spinlock_release(&space->lock, irq_state);
    return ok;
}

bool aspace_user_range_valid(struct address_space *space, uintptr_t address, size_t length, bool write)
{
    if (!range_in_user_space(address, length)) {
        return false;
    }
    if (length == 0) {
        return true;
    }
    unsigned required = ARCH_MAP_USER | (write ? ARCH_MAP_WRITE : 0);
    uintptr_t page = address & ~(ARCH_PAGE_SIZE - 1);
    uintptr_t last_page = (address + length - 1) & ~(ARCH_PAGE_SIZE - 1);

    arch_irq_state_t irq_state = spinlock_acquire(&space->lock);
    bool valid = true;
    for (;; page += ARCH_PAGE_SIZE) {
        unsigned flags;
        if (!arch_mmu_translate(&space->arch, page, NULL, &flags) || (flags & required) != required) {
            valid = false;
            break;
        }
        if (page == last_page) {
            break;
        }
    }
    spinlock_release(&space->lock, irq_state);
    return valid;
}
