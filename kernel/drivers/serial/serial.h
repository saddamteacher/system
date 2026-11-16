#ifndef DRIVERS_SERIAL_H
#define DRIVERS_SERIAL_H

#include <stddef.h>

/*
 * Serial port driver for the platform's debug UART (16550-compatible).
 * Output only for now, by polling.
 *
 * Concurrency: the driver keeps no state of its own, but the device does.
 * Callers must serialise serial_write(); the kernel log is the only caller
 * and does so with its lock.
 */

/* Programs the UART: 115200 baud, 8 data bits, no parity, 1 stop bit. */
void serial_init(void);

/* Blocks until all bytes are handed to the UART. "\n" is sent as "\r\n". */
void serial_write(const char *text, size_t length);

#endif
