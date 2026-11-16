/*
 * PS/2 keyboard: decodes scancode set 1 into key codes.
 *
 * In set 1 a key press sends one byte (the "make" code) and its release
 * sends the same byte with bit 7 set. Keys added after the original PC
 * keyboard (arrows, right control, ...) send the prefix 0xE0 first.
 *
 * State: `extended` and `bytes_to_skip` are touched only by the keyboard
 * interrupt handler.
 */
#include <drivers/keyboard/keyboard.h>

#include <drivers/ps2/i8042.h>

#define SCANCODE_EXTENDED_PREFIX 0xE0
/* Pause sends a 6-byte sequence starting with 0xE1 and has no release
 * code. We do not use the key; the sequence is skipped. */
#define SCANCODE_PAUSE_PREFIX    0xE1
#define PAUSE_SEQUENCE_REMAINDER 5
#define SCANCODE_RELEASE_BIT     0x80

static const uint8_t plain_keys[0x59] = {
    [0x01] = KEY_ESCAPE,
    [0x02] = KEY_1, [0x03] = KEY_2, [0x04] = KEY_3, [0x05] = KEY_4, [0x06] = KEY_5,
    [0x07] = KEY_6, [0x08] = KEY_7, [0x09] = KEY_8, [0x0A] = KEY_9, [0x0B] = KEY_0,
    [0x0C] = KEY_MINUS, [0x0D] = KEY_EQUAL, [0x0E] = KEY_BACKSPACE, [0x0F] = KEY_TAB,
    [0x10] = KEY_Q, [0x11] = KEY_W, [0x12] = KEY_E, [0x13] = KEY_R, [0x14] = KEY_T,
    [0x15] = KEY_Y, [0x16] = KEY_U, [0x17] = KEY_I, [0x18] = KEY_O, [0x19] = KEY_P,
    [0x1A] = KEY_LEFT_BRACKET, [0x1B] = KEY_RIGHT_BRACKET, [0x1C] = KEY_ENTER,
    [0x1D] = KEY_LEFT_CONTROL,
    [0x1E] = KEY_A, [0x1F] = KEY_S, [0x20] = KEY_D, [0x21] = KEY_F, [0x22] = KEY_G,
    [0x23] = KEY_H, [0x24] = KEY_J, [0x25] = KEY_K, [0x26] = KEY_L,
    [0x27] = KEY_SEMICOLON, [0x28] = KEY_APOSTROPHE, [0x29] = KEY_GRAVE,
    [0x2A] = KEY_LEFT_SHIFT, [0x2B] = KEY_BACKSLASH,
    [0x2C] = KEY_Z, [0x2D] = KEY_X, [0x2E] = KEY_C, [0x2F] = KEY_V, [0x30] = KEY_B,
    [0x31] = KEY_N, [0x32] = KEY_M,
    [0x33] = KEY_COMMA, [0x34] = KEY_PERIOD, [0x35] = KEY_SLASH, [0x36] = KEY_RIGHT_SHIFT,
    [0x38] = KEY_LEFT_ALT, [0x39] = KEY_SPACE, [0x3A] = KEY_CAPS_LOCK,
    [0x3B] = KEY_F1, [0x3C] = KEY_F2, [0x3D] = KEY_F3, [0x3E] = KEY_F4, [0x3F] = KEY_F5,
    [0x40] = KEY_F6, [0x41] = KEY_F7, [0x42] = KEY_F8, [0x43] = KEY_F9, [0x44] = KEY_F10,
    [0x57] = KEY_F11, [0x58] = KEY_F12,
};

static bool extended;
static unsigned bytes_to_skip;

static enum key_code extended_key(uint8_t code)
{
    switch (code) {
    case 0x1D: return KEY_RIGHT_CONTROL;
    case 0x38: return KEY_RIGHT_ALT;
    case 0x47: return KEY_HOME;
    case 0x48: return KEY_UP;
    case 0x49: return KEY_PAGE_UP;
    case 0x4B: return KEY_LEFT;
    case 0x4D: return KEY_RIGHT;
    case 0x4F: return KEY_END;
    case 0x50: return KEY_DOWN;
    case 0x51: return KEY_PAGE_DOWN;
    case 0x52: return KEY_INSERT;
    case 0x53: return KEY_DELETE;
    case 0x5B: return KEY_LEFT_SUPER;
    case 0x5C: return KEY_RIGHT_SUPER;
    default:   return KEY_NONE;
    }
}

static void on_byte(uint8_t byte)
{
    if (bytes_to_skip > 0) {
        bytes_to_skip--;
        return;
    }
    if (byte == SCANCODE_PAUSE_PREFIX) {
        bytes_to_skip = PAUSE_SEQUENCE_REMAINDER;
        return;
    }
    if (byte == SCANCODE_EXTENDED_PREFIX) {
        extended = true;
        return;
    }

    bool pressed = (byte & SCANCODE_RELEASE_BIT) == 0;
    uint8_t code = byte & (uint8_t)~SCANCODE_RELEASE_BIT;

    enum key_code key = KEY_NONE;
    if (extended) {
        key = extended_key(code);
    } else if (code < sizeof(plain_keys)) {
        key = plain_keys[code];
    }
    extended = false;

    /* Unknown codes (and the "fake shift" codes some extended keys send)
     * map to KEY_NONE, which keyboard_report() ignores. */
    keyboard_report(key, pressed);
}

bool keyboard_init(void)
{
    return i8042_init() && i8042_attach_keyboard(on_byte);
}
