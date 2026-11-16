/*
 * PS/2 mouse. It reports in packets of three bytes, or four once the wheel
 * has been switched on:
 *
 *   byte 0   bit 0-2 left/right/middle button, bit 3 always 1,
 *            bit 4/5 sign of x/y, bit 6/7 x/y overflow
 *   byte 1   x movement (low 8 bits; the sign is in byte 0)
 *   byte 2   y movement, positive = up
 *   byte 3   wheel movement (4-bit signed), only in wheel mode
 *
 * State: `packet`, `packet_length` and `packet_size` are touched only by
 * the interrupt handler after setup.
 */
#include <drivers/mouse/mouse.h>

#include <stdint.h>

#include <drivers/ps2/i8042.h>

#define COMMAND_GET_ID            0xF2
#define COMMAND_SET_SAMPLE_RATE   0xF3
#define COMMAND_ENABLE_REPORTING  0xF4
#define COMMAND_SET_DEFAULTS      0xF6

#define ID_WHEEL_MOUSE 0x03

#define PACKET_ALWAYS_ONE (1u << 3)
#define PACKET_X_SIGN     (1u << 4)
#define PACKET_Y_SIGN     (1u << 5)
#define PACKET_OVERFLOW   ((1u << 6) | (1u << 7))
#define PACKET_BUTTONS    0x07

static uint8_t packet[4];
static unsigned packet_length;
static unsigned packet_size = 3;

static bool set_sample_rate(uint8_t rate)
{
    return i8042_mouse_command(COMMAND_SET_SAMPLE_RATE) && i8042_mouse_command(rate);
}

/*
 * The wheel is an extension that stays off until the driver "knocks":
 * setting the sample rate to 200, 100 and 80 in a row makes a wheel mouse
 * change its device id to 3 and start sending four-byte packets. A plain
 * mouse ignores the sequence.
 */
static bool enable_wheel(void)
{
    uint8_t id;
    return set_sample_rate(200) && set_sample_rate(100) && set_sample_rate(80)
        && i8042_mouse_command(COMMAND_GET_ID) && i8042_mouse_read(&id) && id == ID_WHEEL_MOUSE;
}

static void handle_packet(void)
{
    if (packet[0] & PACKET_OVERFLOW) {
        return; /* the movement values are meaningless */
    }
    /* Movement is a 9-bit two's complement number: 8 bits in the data
     * byte, the sign bit in byte 0. */
    int dx = packet[1] - ((packet[0] & PACKET_X_SIGN) ? 256 : 0);
    int dy = packet[2] - ((packet[0] & PACKET_Y_SIGN) ? 256 : 0);
    int wheel = 0;
    if (packet_size == 4) {
        /* Sign-extend the low 4 bits. */
        wheel = (packet[3] & 0x0F) - ((packet[3] & 0x08) ? 16 : 0);
    }
    /* The mouse counts y upwards; the screen counts it downwards. */
    mouse_report(dx, -dy, wheel, packet[0] & PACKET_BUTTONS);
}

static void on_byte(uint8_t byte)
{
    /* If we ever lose a byte, every following packet would be shifted.
     * Bit 3 of the first byte is always set, which lets us find a packet
     * start again. */
    if (packet_length == 0 && (byte & PACKET_ALWAYS_ONE) == 0) {
        return;
    }
    packet[packet_length++] = byte;
    if (packet_length == packet_size) {
        packet_length = 0;
        handle_packet();
    }
}

bool mouse_init(void)
{
    if (!i8042_enable_mouse_port() || !i8042_mouse_command(COMMAND_SET_DEFAULTS)) {
        return false;
    }
    if (enable_wheel()) {
        packet_size = 4;
    }
    return i8042_mouse_command(COMMAND_ENABLE_REPORTING) && i8042_attach_mouse(on_byte);
}
