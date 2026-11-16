#include <arch/ps2.h>

#include "io.h"

/* The 8042 controller: one port for data, one for status (read) and
 * commands (write). */
#define PS2_DATA_PORT    0x60
#define PS2_COMMAND_PORT 0x64

uint8_t arch_ps2_read_data(void)
{
    return io_in8(PS2_DATA_PORT);
}

void arch_ps2_write_data(uint8_t value)
{
    io_out8(PS2_DATA_PORT, value);
}

uint8_t arch_ps2_read_status(void)
{
    return io_in8(PS2_COMMAND_PORT);
}

void arch_ps2_write_command(uint8_t command)
{
    io_out8(PS2_COMMAND_PORT, command);
}
