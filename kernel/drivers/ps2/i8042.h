#ifndef DRIVERS_PS2_I8042_H
#define DRIVERS_PS2_I8042_H

#include <stdbool.h>
#include <stdint.h>

/*
 * Driver for the PS/2 controller (Intel 8042 compatible), the chip between
 * the CPU and the PS/2 keyboard and mouse. It only moves bytes; what the
 * bytes mean is up to the device drivers attached to its ports.
 */

/* Brings the controller into a known state with both ports disabled.
 * Returns false if it does not respond. */
bool i8042_init(void);

/* Called in interrupt context for every byte a device sends. */
typedef void (*i8042_byte_handler_fn)(uint8_t byte);

/* Enables the keyboard port and delivers its bytes to `handler`. */
bool i8042_attach_keyboard(i8042_byte_handler_fn handler);

/* Lets the mouse talk to the controller (needed before configuring it). */
bool i8042_enable_mouse_port(void);

/* Enables the mouse interrupt and delivers its bytes to `handler`. */
bool i8042_attach_mouse(i8042_byte_handler_fn handler);

/*
 * Sends a command byte to the mouse and waits for its acknowledgement.
 * Only for use during driver setup, before the mouse is attached: the
 * reply is read by polling.
 */
bool i8042_mouse_command(uint8_t command);
/* Reads one reply byte from the mouse by polling; false on timeout. */
bool i8042_mouse_read(uint8_t *byte);

#endif
