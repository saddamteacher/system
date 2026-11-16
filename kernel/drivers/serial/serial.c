#include <drivers/serial/serial.h>

#include <stdint.h>

#include <arch/cpu.h>
#include <arch/debug_uart.h>

/* 16550 register indices. */
#define REG_DATA          0 /* transmit/receive; divisor low byte when DLAB=1 */
#define REG_INT_ENABLE    1 /* interrupt enable; divisor high byte when DLAB=1 */
#define REG_FIFO_CONTROL  2
#define REG_LINE_CONTROL  3
#define REG_MODEM_CONTROL 4
#define REG_LINE_STATUS   5

#define LINE_CONTROL_8N1 0x03 /* 8 data bits, no parity, 1 stop bit */
/* Divisor Latch Access Bit: while set, registers 0 and 1 are the baud rate
 * divisor instead of data and interrupt enable. */
#define LINE_CONTROL_DLAB 0x80

#define FIFO_ENABLE_AND_CLEAR 0x07
#define MODEM_DTR_RTS 0x03 /* tell the other end we are ready */
#define LINE_STATUS_TX_EMPTY 0x20 /* transmit holding register is free */

/* The UART's reference clock allows at most 115200 baud;
 * baud rate = 115200 / divisor. */
#define BAUD_DIVISOR 1

void serial_init(void)
{
    /* We poll, so the UART must not raise interrupts nobody handles. */
    arch_debug_uart_write(REG_INT_ENABLE, 0x00);

    arch_debug_uart_write(REG_LINE_CONTROL, LINE_CONTROL_DLAB);
    arch_debug_uart_write(REG_DATA, BAUD_DIVISOR & 0xFF);
    arch_debug_uart_write(REG_INT_ENABLE, BAUD_DIVISOR >> 8);
    arch_debug_uart_write(REG_LINE_CONTROL, LINE_CONTROL_8N1);

    arch_debug_uart_write(REG_FIFO_CONTROL, FIFO_ENABLE_AND_CLEAR);
    arch_debug_uart_write(REG_MODEM_CONTROL, MODEM_DTR_RTS);
}

static void write_byte(uint8_t byte)
{
    /* Writing while the holding register is full would drop the byte. */
    while ((arch_debug_uart_read(REG_LINE_STATUS) & LINE_STATUS_TX_EMPTY) == 0) {
        arch_cpu_relax();
    }
    arch_debug_uart_write(REG_DATA, byte);
}

void serial_write(const char *text, size_t length)
{
    for (size_t i = 0; i < length; i++) {
        /* Terminals need a carriage return to go back to column 0. */
        if (text[i] == '\n') {
            write_byte('\r');
        }
        write_byte((uint8_t)text[i]);
    }
}
