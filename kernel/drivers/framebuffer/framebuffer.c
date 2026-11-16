/*
 * State: `display` and `frames` are set once in framebuffer_init().
 * `mapped` says whether some address space currently has the display
 * mapped; it is atomic because the release callback may run in any thread.
 */
#include <drivers/framebuffer/framebuffer.h>

#include <stdatomic.h>

#include <saddev/errno.h>

#include <arch/boot.h>
#include <arch/mmu.h>
#include <core/log.h>
#include <mm/aspace.h>
#include <mm/heap.h>

static struct arch_boot_framebuffer display;
static bool present;
static phys_addr_t *frames;
static size_t frame_count;
static atomic_flag mapped = ATOMIC_FLAG_INIT;

bool framebuffer_init(void)
{
    if (!arch_boot_framebuffer(&display)) {
        log_warn("framebuffer: the bootloader provided no display");
        return false;
    }
    if (!display.is_xrgb8888) {
        log_warn("framebuffer: unsupported pixel format (%u bits per pixel)", display.bits_per_pixel);
        return false;
    }

    /* The address space code maps lists of frames; describe the display
     * memory that way once, up front. */
    size_t bytes = (size_t)display.pitch * display.height;
    frame_count = (bytes + ARCH_PAGE_SIZE - 1) / ARCH_PAGE_SIZE;
    frames = kmalloc(frame_count * sizeof(phys_addr_t));
    if (frames == NULL) {
        return false;
    }
    for (size_t i = 0; i < frame_count; i++) {
        frames[i] = display.address + i * ARCH_PAGE_SIZE;
    }
    present = true;
    log_info("framebuffer: %ux%u, %u bytes per row, at %p", display.width, display.height, display.pitch,
             (void *)display.address);
    return true;
}

bool framebuffer_info(struct sd_fb_info *info)
{
    if (!present) {
        return false;
    }
    *info = (struct sd_fb_info){ .width = display.width, .height = display.height, .pitch = display.pitch };
    return true;
}

static void on_unmapped(void *context)
{
    (void)context;
    atomic_flag_clear(&mapped);
}

uintptr_t framebuffer_map(struct address_space *space, int *error)
{
    if (!present) {
        *error = -ENODEV;
        return 0;
    }
    /* Two programs drawing to the screen behind each other's back would
     * only produce garbage. */
    if (atomic_flag_test_and_set(&mapped)) {
        *error = -EBUSY;
        return 0;
    }
    uintptr_t address = aspace_map_frames(space, frames, frame_count, on_unmapped, NULL);
    if (address == 0) {
        atomic_flag_clear(&mapped);
        *error = -ENOMEM;
    }
    return address;
}
