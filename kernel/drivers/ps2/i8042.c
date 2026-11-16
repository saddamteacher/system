/*
 * State: keyboard_handler and mouse_handler are each written once, by the
 * attach function, before that device's interrupt is enabled; the interrupt
 * handler reads them.
 */
#include <drivers/ps2/i8042.h>

#include <stddef.h>

#include <arch/cpu.h>
#include <arch/irq.h>
#include <arch/ps2.h>

/* Status register bits. "Output" and "input" are named from the
 * controller's point of view: its output buffer is what we read. */
#define STATUS_OUTPUT_FULL (1u << 0) /* a byte is waiting for us */
#define STATUS_INPUT_FULL  (1u << 1) /* controller has not consumed our last write */
#define STATUS_FROM_MOUSE  (1u << 5) /* the waiting byte came from the second port */

#define COMMAND_READ_CONFIG     0x20
#define COMMAND_WRITE_CONFIG    0x60
#define COMMAND_DISABLE_MOUSE   0xA7
#define COMMAND_DISABLE_KEYBOARD 0xAD
#define COMMAND_ENABLE_KEYBOARD 0xAE
#define COMMAND_ENABLE_MOUSE    0xA8
/* "The next byte written to the data port goes to the mouse." */
#define COMMAND_WRITE_TO_MOUSE  0xD4

#define MOUSE_ACKNOWLEDGE 0xFA

#define CONFIG_KEYBOARD_IRQ     (1u << 0)
#define CONFIG_MOUSE_IRQ        (1u << 1)
#define CONFIG_KEYBOARD_CLOCK_OFF (1u << 4)
#define CONFIG_MOUSE_CLOCK_OFF  (1u << 5)
/* Translate whatever scancode set the keyboard uses into set 1, the one
 * the keyboard driver decodes. */
#define CONFIG_TRANSLATION      (1u << 6)

/* The controller answers within microseconds; this bound only exists so a
 * missing or broken controller cannot hang the boot. */
#define WAIT_ATTEMPTS 100000

static i8042_byte_handler_fn keyboard_handler;
static i8042_byte_handler_fn mouse_handler;

static bool wait_until_writable(void)
{
    for (int attempt = 0; attempt < WAIT_ATTEMPTS; attempt++) {
        if ((arch_ps2_read_status() & STATUS_INPUT_FULL) == 0) {
            return true;
        }
        arch_cpu_relax();
    }
    return false;
}

static bool wait_until_readable(void)
{
    for (int attempt = 0; attempt < WAIT_ATTEMPTS; attempt++) {
        if ((arch_ps2_read_status() & STATUS_OUTPUT_FULL) != 0) {
            return true;
        }
        arch_cpu_relax();
    }
    return false;
}

static bool send_command(uint8_t command)
{
    if (!wait_until_writable()) {
        return false;
    }
    arch_ps2_write_command(command);
    return true;
}

/* Throws away bytes left over from before we took control (for example a
 * key pressed in the boot menu). A stale byte would otherwise be decoded as
 * the first half of a later scancode. */
static void flush_output_buffer(void)
{
    while ((arch_ps2_read_status() & STATUS_OUTPUT_FULL) != 0) {
        (void)arch_ps2_read_data();
    }
}

static bool read_config(uint8_t *config)
{
    if (!send_command(COMMAND_READ_CONFIG) || !wait_until_readable()) {
        return false;
    }
    *config = arch_ps2_read_data();
    return true;
}

static bool write_config(uint8_t config)
{
    if (!send_command(COMMAND_WRITE_CONFIG) || !wait_until_writable()) {
        return false;
    }
    arch_ps2_write_data(config);
    return true;
}

bool i8042_init(void)
{
    /* Keep the devices quiet while we reconfigure. */
    if (!send_command(COMMAND_DISABLE_KEYBOARD) || !send_command(COMMAND_DISABLE_MOUSE)) {
        return false;
    }
    flush_output_buffer();

    uint8_t config;
    if (!read_config(&config)) {
        return false;
    }
    config &= (uint8_t)~(CONFIG_KEYBOARD_IRQ | CONFIG_MOUSE_IRQ);
    config |= CONFIG_TRANSLATION;
    return write_config(config);
}

/* Shared by both interrupt lines: the controller has one output buffer for
 * both devices, and a status bit says where each byte came from. */
static void device_irq(void *context)
{
    (void)context;

    /* Drain everything that is waiting: more than one byte may have arrived
     * before we got to run. */
    for (;;) {
        uint8_t status = arch_ps2_read_status();
        if ((status & STATUS_OUTPUT_FULL) == 0) {
            return;
        }
        uint8_t byte = arch_ps2_read_data();
        i8042_byte_handler_fn handler = (status & STATUS_FROM_MOUSE) != 0 ? mouse_handler : keyboard_handler;
        if (handler != NULL) {
            handler(byte);
        }
    }
}

bool i8042_attach_keyboard(i8042_byte_handler_fn handler)
{
    keyboard_handler = handler;
    if (!arch_irq_register(ARCH_PS2_KEYBOARD_IRQ, device_irq, NULL)) {
        return false;
    }

    uint8_t config;
    if (!read_config(&config)) {
        return false;
    }
    config |= CONFIG_KEYBOARD_IRQ;
    config &= (uint8_t)~CONFIG_KEYBOARD_CLOCK_OFF;
    return write_config(config) && send_command(COMMAND_ENABLE_KEYBOARD);
}

bool i8042_mouse_read(uint8_t *byte)
{
    if (!wait_until_readable()) {
        return false;
    }
    *byte = arch_ps2_read_data();
    return true;
}

bool i8042_mouse_command(uint8_t command)
{
    if (!send_command(COMMAND_WRITE_TO_MOUSE) || !wait_until_writable()) {
        return false;
    }
    arch_ps2_write_data(command);

    uint8_t reply;
    return i8042_mouse_read(&reply) && reply == MOUSE_ACKNOWLEDGE;
}

bool i8042_attach_mouse(i8042_byte_handler_fn handler)
{
    mouse_handler = handler;
    if (!arch_irq_register(ARCH_PS2_MOUSE_IRQ, device_irq, NULL)) {
        return false;
    }
    uint8_t config;
    if (!read_config(&config)) {
        return false;
    }
    config |= CONFIG_MOUSE_IRQ;
    config &= (uint8_t)~CONFIG_MOUSE_CLOCK_OFF;
    return write_config(config);
}

bool i8042_enable_mouse_port(void)
{
    return send_command(COMMAND_ENABLE_MOUSE);
}
