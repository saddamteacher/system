#ifndef DRIVERS_FRAMEBUFFER_H
#define DRIVERS_FRAMEBUFFER_H

#include <stdbool.h>
#include <stdint.h>

#include <saddev/input.h>

/*
 * The display, as a linear framebuffer set up by the bootloader. The kernel
 * does not draw: it describes the display and lets one user process (the
 * window server) map its memory.
 */

struct address_space;

/* Returns false if there is no usable display. */
bool framebuffer_init(void);

/* Describes the display; false if there is none. */
bool framebuffer_info(struct sd_fb_info *info);

/*
 * Maps the display memory into an address space and returns the address.
 * Only one address space may hold the mapping at a time; it is released
 * when the mapping is removed or the address space destroyed.
 * Returns 0 and sets *error to a negative code on failure.
 */
uintptr_t framebuffer_map(struct address_space *space, int *error);

#endif
