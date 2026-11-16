/*
 * Only PIT channel 2 is used. Unlike channel 0 it is not wired to an
 * interrupt line, so it can serve as a plain readable counter while
 * interrupts are off.
 */
#include "pit.h"

#include "io.h"

#define PIT_CHANNEL2_DATA 0x42
#define PIT_COMMAND       0x43
/* "Keyboard controller port B": bit 0 gates channel 2's clock, bit 1 routes
 * its output to the PC speaker. */
#define PORT_B            0x61
#define PORT_B_GATE2      0x01
#define PORT_B_SPEAKER    0x02

/* Channel 2, write low byte then high byte, mode 0 (count down once). */
#define COMMAND_CHANNEL2_ONESHOT 0xB0
/* Copies channel 2's running count into a latch, so that the two bytes read
 * afterwards belong to the same instant. */
#define COMMAND_CHANNEL2_LATCH   0x80

void pit_start_countdown(void)
{
    uint8_t port_b = io_in8(PORT_B);
    io_out8(PORT_B, (uint8_t)((port_b & ~PORT_B_SPEAKER) | PORT_B_GATE2));

    io_out8(PIT_COMMAND, COMMAND_CHANNEL2_ONESHOT);
    io_out8(PIT_CHANNEL2_DATA, PIT_COUNTDOWN_START & 0xFF);
    /* Counting starts when the high byte is written. */
    io_out8(PIT_CHANNEL2_DATA, PIT_COUNTDOWN_START >> 8);
}

uint16_t pit_read_countdown(void)
{
    io_out8(PIT_COMMAND, COMMAND_CHANNEL2_LATCH);
    uint8_t low = io_in8(PIT_CHANNEL2_DATA);
    uint8_t high = io_in8(PIT_CHANNEL2_DATA);
    return (uint16_t)(low | (high << 8));
}
