#ifndef SADDEV_INPUT_H
#define SADDEV_INPUT_H

/*
 * Input events, as read from /dev/keyboard and /dev/mouse. A read returns
 * as many whole events as fit in the buffer (at least one; it blocks while
 * there is none). Both devices can be polled.
 */

#include <stdint.h>

/* Physical keys, named after their US layout legend. */
enum key_code {
    KEY_NONE = 0,
    KEY_ESCAPE, KEY_F1, KEY_F2, KEY_F3, KEY_F4, KEY_F5, KEY_F6,
    KEY_F7, KEY_F8, KEY_F9, KEY_F10, KEY_F11, KEY_F12,
    KEY_GRAVE, KEY_1, KEY_2, KEY_3, KEY_4, KEY_5, KEY_6, KEY_7, KEY_8, KEY_9, KEY_0,
    KEY_MINUS, KEY_EQUAL, KEY_BACKSPACE,
    KEY_TAB, KEY_Q, KEY_W, KEY_E, KEY_R, KEY_T, KEY_Y, KEY_U, KEY_I, KEY_O, KEY_P,
    KEY_LEFT_BRACKET, KEY_RIGHT_BRACKET, KEY_BACKSLASH,
    KEY_CAPS_LOCK, KEY_A, KEY_S, KEY_D, KEY_F, KEY_G, KEY_H, KEY_J, KEY_K, KEY_L,
    KEY_SEMICOLON, KEY_APOSTROPHE, KEY_ENTER,
    KEY_LEFT_SHIFT, KEY_Z, KEY_X, KEY_C, KEY_V, KEY_B, KEY_N, KEY_M,
    KEY_COMMA, KEY_PERIOD, KEY_SLASH, KEY_RIGHT_SHIFT,
    KEY_LEFT_CONTROL, KEY_LEFT_SUPER, KEY_LEFT_ALT, KEY_SPACE,
    KEY_RIGHT_ALT, KEY_RIGHT_SUPER, KEY_RIGHT_CONTROL,
    KEY_INSERT, KEY_DELETE, KEY_HOME, KEY_END, KEY_PAGE_UP, KEY_PAGE_DOWN,
    KEY_UP, KEY_DOWN, KEY_LEFT, KEY_RIGHT,
    KEY_CODE_COUNT
};

/* Modifier state at the time of a key event. */
#define KEY_MODIFIER_SHIFT     (1u << 0)
#define KEY_MODIFIER_CONTROL   (1u << 1)
#define KEY_MODIFIER_ALT       (1u << 2)
#define KEY_MODIFIER_SUPER     (1u << 3)
#define KEY_MODIFIER_CAPS_LOCK (1u << 4)

struct sd_key_event {
    uint16_t code;     /* enum key_code */
    uint8_t pressed;   /* 1 = went down, 0 = released */
    uint8_t modifiers; /* KEY_MODIFIER_* */
    /* The character the key produces with the current modifiers (US
     * layout), or 0 for keys that produce none. Set on release too. */
    char character;
    uint8_t reserved[3];
};

#define MOUSE_BUTTON_LEFT   (1u << 0)
#define MOUSE_BUTTON_RIGHT  (1u << 1)
#define MOUSE_BUTTON_MIDDLE (1u << 2)

/* Relative motion since the previous event, in device units, with y
 * growing downwards as on the screen. */
struct sd_mouse_event {
    int16_t dx;
    int16_t dy;
    int8_t wheel;    /* positive = scrolled down */
    uint8_t buttons; /* MOUSE_BUTTON_* currently held */
    uint16_t reserved;
};

/*
 * The display, as reported by SYS_FB_INFO. Pixels are 32 bits each, stored
 * as 0x00RRGGBB in a little-endian word; `pitch` is the distance in bytes
 * between the starts of two rows and may exceed width * 4.
 */
struct sd_fb_info {
    uint32_t width;
    uint32_t height;
    uint32_t pitch;
    uint32_t reserved;
};

#endif
