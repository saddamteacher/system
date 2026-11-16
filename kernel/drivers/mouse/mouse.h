#ifndef DRIVERS_MOUSE_H
#define DRIVERS_MOUSE_H

#include <stdbool.h>

#include <saddev/input.h>

/*
 * Mouse input: relative motion, buttons and the scroll wheel, queued as
 * events for a consumer to read.
 */

/* Sets up the PS/2 mouse. Returns false if there is none. */
bool mouse_init(void);

/* Takes the oldest pending event; false if there is none. Never blocks. */
bool mouse_read_event(struct sd_mouse_event *event);
bool mouse_has_event(void);

/* Installs a function called, in interrupt context, whenever an event was
 * queued. One listener; set during boot. */
void mouse_set_listener(void (*listener)(void));

/* For hardware drivers: reports movement and the current button state.
 * Called in interrupt context. */
void mouse_report(int dx, int dy, int wheel, unsigned buttons);

#endif
