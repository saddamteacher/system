#ifndef ARCH_DEBUG_UART_H
#define ARCH_DEBUG_UART_H

#include <stdint.h>

/*
 * Register access to the platform's debug UART, a 16550-compatible device.
 *
 * The 16550 register set is the same everywhere; how the registers are
 * reached is not (I/O ports on a PC, memory-mapped elsewhere). The serial
 * driver in kernel/drivers owns the device logic and uses these two functions
 * for the platform-specific part. `reg` is the register index 0..7.
 */
uint8_t arch_debug_uart_read(unsigned reg);
void arch_debug_uart_write(unsigned reg, uint8_t value);

#endif
