/*
 * Early console on the first PC serial port (COM1, a 16550-compatible UART).
 * QEMU connects this port to the host terminal with `-serial stdio`.
 */
#include <arch/early_console.h>

#include <stdint.h>

#include "early_console.h"
#include "io.h"

#define COM1_BASE 0x3F8

/* Register offsets from the base port. */
#define UART_DATA          0 /* transmit/receive buffer; divisor low when DLAB=1 */
#define UART_INT_ENABLE    1 /* interrupt enable; divisor high when DLAB=1 */
#define UART_FIFO_CONTROL  2
#define UART_LINE_CONTROL  3
#define UART_MODEM_CONTROL 4
#define UART_LINE_STATUS   5

#define LINE_CONTROL_8N1   0x03 /* 8 data bits, no parity, 1 stop bit */
#define LINE_CONTROL_DLAB  0x80 /* exposes the baud divisor registers */
#define LINE_STATUS_TX_EMPTY 0x20 /* transmit holding register is free */

/* The UART clock is 115200 Hz; baud rate = 115200 / divisor. */
#define BAUD_DIVISOR_115200 1

void early_console_init(void)
{
    /* We poll, so the UART must not raise interrupts nobody handles yet. */
    io_out8(COM1_BASE + UART_INT_ENABLE, 0x00);

    io_out8(COM1_BASE + UART_LINE_CONTROL, LINE_CONTROL_DLAB);
    io_out8(COM1_BASE + UART_DATA, BAUD_DIVISOR_115200 & 0xFF);
    io_out8(COM1_BASE + UART_INT_ENABLE, BAUD_DIVISOR_115200 >> 8);
    io_out8(COM1_BASE + UART_LINE_CONTROL, LINE_CONTROL_8N1);

    /* Enable and clear the FIFOs. */
    io_out8(COM1_BASE + UART_FIFO_CONTROL, 0x07);
    /* Assert DTR and RTS so the other end knows we are ready. */
    io_out8(COM1_BASE + UART_MODEM_CONTROL, 0x03);
}

static void write_byte(uint8_t byte)
{
    /* Writing while the holding register is full would drop the byte. */
    while ((io_in8(COM1_BASE + UART_LINE_STATUS) & LINE_STATUS_TX_EMPTY) == 0) {
    }
    io_out8(COM1_BASE + UART_DATA, byte);
}

void arch_early_console_write(const char *text, size_t length)
{
    for (size_t i = 0; i < length; i++) {
        write_byte((uint8_t)text[i]);
    }
}
