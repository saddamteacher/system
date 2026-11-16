#include <arch/debug_uart.h>

#include "io.h"

/* COM1, the first PC serial port. QEMU connects it to the host terminal
 * with `-serial stdio`. Its eight registers are consecutive I/O ports. */
#define COM1_BASE 0x3F8

uint8_t arch_debug_uart_read(unsigned reg)
{
    return io_in8((uint16_t)(COM1_BASE + reg));
}

void arch_debug_uart_write(unsigned reg, uint8_t value)
{
    io_out8((uint16_t)(COM1_BASE + reg), value);
}
