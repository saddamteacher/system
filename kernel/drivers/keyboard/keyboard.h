#ifndef DRIVERS_KEYBOARD_H
#define DRIVERS_KEYBOARD_H

#include <stdbool.h>
#include <stdint.h>

/*
 * Keyboard input, independent of how the keyboard is attached. Hardware
 * specific drivers (ps2_keyboard.c) report raw key transitions; consumers
 * read finished events from a queue.
 */

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

/* Modifier state at the time of an event. */
#define KEY_MODIFIER_SHIFT     (1u << 0)
#define KEY_MODIFIER_CONTROL   (1u << 1)
#define KEY_MODIFIER_ALT       (1u << 2)
#define KEY_MODIFIER_SUPER     (1u << 3)
#define KEY_MODIFIER_CAPS_LOCK (1u << 4)

struct key_event {
    uint16_t code; /* enum key_code */
    bool pressed;  /* false = released */
    uint8_t modifiers;
    /* The character the key produces with the current modifiers (US
     * layout), or 0 for keys that produce none. Set on release too. */
    char character;
};

/* Sets up the PS/2 keyboard. Returns false if there is none. */
bool keyboard_init(void);

/* Takes the oldest pending event. Returns false if there is none. Never
 * blocks; safe to call from any context. */
bool keyboard_read_event(struct key_event *event);

/* Like keyboard_read_event(), but sleeps until an event is available.
 * Thread context only. */
void keyboard_wait_event(struct key_event *event);

/* For hardware drivers: reports that a key went down or up. Called in
 * interrupt context. */
void keyboard_report(enum key_code code, bool pressed);

/* Writes one line describing the event to the kernel log. */
void keyboard_log_event(const struct key_event *event);

/* Short name of a key for diagnostics, e.g. "enter". */
const char *keyboard_key_name(enum key_code code);

#endif
