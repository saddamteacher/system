#ifndef ARCH_PS2_H
#define ARCH_PS2_H

#include <stdint.h>

/*
 * Register access to the PS/2 controller (Intel 8042 compatible). The
 * controller logic lives in kernel/drivers/ps2; this is only the
 * platform-specific way to reach its two registers.
 */
uint8_t arch_ps2_read_data(void);
void arch_ps2_write_data(uint8_t value);
uint8_t arch_ps2_read_status(void);
void arch_ps2_write_command(uint8_t command);

/* IRQ lines of the controller's two ports. */
#define ARCH_PS2_KEYBOARD_IRQ 1
#define ARCH_PS2_MOUSE_IRQ    12

#endif
