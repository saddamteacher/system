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

#endif
