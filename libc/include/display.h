#ifndef _DISPLAY_H
#define _DISPLAY_H

#include <saddev/input.h>

/* Describes the display. Returns 0, or -1 with errno set (ENODEV if there
 * is none). */
int display_info(struct sd_fb_info *info);

/* Maps the display's pixel memory into this process and returns it, or
 * NULL with errno set (EBUSY if another process has it). */
void *display_map(void);

#endif
