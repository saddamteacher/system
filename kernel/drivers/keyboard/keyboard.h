#ifndef DRIVERS_KEYBOARD_H
#define DRIVERS_KEYBOARD_H

#include <stdbool.h>
#include <stdint.h>

#include <saddev/input.h>

/*
 * Keyboard input, independent of how the keyboard is attached. Hardware
 * specific drivers (ps2_keyboard.c) report raw key transitions; consumers
 * read finished events from a queue.
 */

/* Sets up the PS/2 keyboard. Returns false if there is none. */
bool keyboard_init(void);

/* Takes the oldest pending event. Returns false if there is none. Never
 * blocks; safe to call from any context. */
bool keyboard_read_event(struct sd_key_event *event);

/* True if keyboard_read_event() would return an event. */
bool keyboard_has_event(void);

/* Like keyboard_read_event(), but sleeps until an event is available.
 * Thread context only. */
void keyboard_wait_event(struct sd_key_event *event);

/* Installs a function called, in interrupt context, whenever an event was
 * queued. One listener; set during boot. Lets a higher layer learn about
 * input without the driver depending on it. */
void keyboard_set_listener(void (*listener)(void));

/* For hardware drivers: reports that a key went down or up. Called in
 * interrupt context. */
void keyboard_report(enum key_code code, bool pressed);

/* Writes one line describing the event to the kernel log. */
void keyboard_log_event(const struct sd_key_event *event);

/* Short name of a key for diagnostics, e.g. "enter". */
const char *keyboard_key_name(enum key_code code);

#endif
