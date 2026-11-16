/*
 * Device-independent half of keyboard input: tracks modifiers, translates
 * keys to characters and queues events.
 *
 * State (protected by `lock`): the event queue and the modifier state. The
 * writer is the keyboard interrupt, the readers are threads.
 * Lock order: keyboard lock, then the scheduler lock (inside the wait queue).
 */
#include <drivers/keyboard/keyboard.h>

#include <stddef.h>

#include <core/log.h>
#include <core/spinlock.h>
#include <sched/wait_queue.h>

#define QUEUE_CAPACITY 64

struct key_info {
    const char *name;
    char normal;  /* character without shift, 0 if none */
    char shifted; /* character with shift */
};

static const struct key_info key_table[KEY_CODE_COUNT] = {
    [KEY_ESCAPE] = { "escape", 0, 0 },
    [KEY_F1] = { "f1", 0, 0 },   [KEY_F2] = { "f2", 0, 0 },   [KEY_F3] = { "f3", 0, 0 },
    [KEY_F4] = { "f4", 0, 0 },   [KEY_F5] = { "f5", 0, 0 },   [KEY_F6] = { "f6", 0, 0 },
    [KEY_F7] = { "f7", 0, 0 },   [KEY_F8] = { "f8", 0, 0 },   [KEY_F9] = { "f9", 0, 0 },
    [KEY_F10] = { "f10", 0, 0 }, [KEY_F11] = { "f11", 0, 0 }, [KEY_F12] = { "f12", 0, 0 },
    [KEY_GRAVE] = { "`", '`', '~' },
    [KEY_1] = { "1", '1', '!' }, [KEY_2] = { "2", '2', '@' }, [KEY_3] = { "3", '3', '#' },
    [KEY_4] = { "4", '4', '$' }, [KEY_5] = { "5", '5', '%' }, [KEY_6] = { "6", '6', '^' },
    [KEY_7] = { "7", '7', '&' }, [KEY_8] = { "8", '8', '*' }, [KEY_9] = { "9", '9', '(' },
    [KEY_0] = { "0", '0', ')' },
    [KEY_MINUS] = { "-", '-', '_' }, [KEY_EQUAL] = { "=", '=', '+' },
    [KEY_BACKSPACE] = { "backspace", '\b', '\b' },
    [KEY_TAB] = { "tab", '\t', '\t' },
    [KEY_Q] = { "q", 'q', 'Q' }, [KEY_W] = { "w", 'w', 'W' }, [KEY_E] = { "e", 'e', 'E' },
    [KEY_R] = { "r", 'r', 'R' }, [KEY_T] = { "t", 't', 'T' }, [KEY_Y] = { "y", 'y', 'Y' },
    [KEY_U] = { "u", 'u', 'U' }, [KEY_I] = { "i", 'i', 'I' }, [KEY_O] = { "o", 'o', 'O' },
    [KEY_P] = { "p", 'p', 'P' },
    [KEY_LEFT_BRACKET] = { "[", '[', '{' }, [KEY_RIGHT_BRACKET] = { "]", ']', '}' },
    [KEY_BACKSLASH] = { "\\", '\\', '|' },
    [KEY_CAPS_LOCK] = { "caps-lock", 0, 0 },
    [KEY_A] = { "a", 'a', 'A' }, [KEY_S] = { "s", 's', 'S' }, [KEY_D] = { "d", 'd', 'D' },
    [KEY_F] = { "f", 'f', 'F' }, [KEY_G] = { "g", 'g', 'G' }, [KEY_H] = { "h", 'h', 'H' },
    [KEY_J] = { "j", 'j', 'J' }, [KEY_K] = { "k", 'k', 'K' }, [KEY_L] = { "l", 'l', 'L' },
    [KEY_SEMICOLON] = { ";", ';', ':' }, [KEY_APOSTROPHE] = { "'", '\'', '"' },
    [KEY_ENTER] = { "enter", '\n', '\n' },
    [KEY_LEFT_SHIFT] = { "left-shift", 0, 0 },
    [KEY_Z] = { "z", 'z', 'Z' }, [KEY_X] = { "x", 'x', 'X' }, [KEY_C] = { "c", 'c', 'C' },
    [KEY_V] = { "v", 'v', 'V' }, [KEY_B] = { "b", 'b', 'B' }, [KEY_N] = { "n", 'n', 'N' },
    [KEY_M] = { "m", 'm', 'M' },
    [KEY_COMMA] = { ",", ',', '<' }, [KEY_PERIOD] = { ".", '.', '>' }, [KEY_SLASH] = { "/", '/', '?' },
    [KEY_RIGHT_SHIFT] = { "right-shift", 0, 0 },
    [KEY_LEFT_CONTROL] = { "left-control", 0, 0 }, [KEY_LEFT_SUPER] = { "left-super", 0, 0 },
    [KEY_LEFT_ALT] = { "left-alt", 0, 0 },         [KEY_SPACE] = { "space", ' ', ' ' },
    [KEY_RIGHT_ALT] = { "right-alt", 0, 0 },       [KEY_RIGHT_SUPER] = { "right-super", 0, 0 },
    [KEY_RIGHT_CONTROL] = { "right-control", 0, 0 },
    [KEY_INSERT] = { "insert", 0, 0 },   [KEY_DELETE] = { "delete", 0, 0 },
    [KEY_HOME] = { "home", 0, 0 },       [KEY_END] = { "end", 0, 0 },
    [KEY_PAGE_UP] = { "page-up", 0, 0 }, [KEY_PAGE_DOWN] = { "page-down", 0, 0 },
    [KEY_UP] = { "up", 0, 0 },     [KEY_DOWN] = { "down", 0, 0 },
    [KEY_LEFT] = { "left", 0, 0 }, [KEY_RIGHT] = { "right", 0, 0 },
};

static struct spinlock lock = SPINLOCK_INIT;
static struct sd_key_event queue[QUEUE_CAPACITY];
static size_t queue_head;  /* index of the oldest event */
static size_t queue_count;
static uint8_t modifiers;
/* Threads blocked in keyboard_wait_event(). Initialised statically because
 * the interrupt handler may use it before any thread does. */
/* Written once during boot. */
static void (*event_listener)(void);
static struct wait_queue readers = { .waiters = { &readers.waiters, &readers.waiters } };

const char *keyboard_key_name(enum key_code code)
{
    if (code >= KEY_CODE_COUNT || key_table[code].name == NULL) {
        return "unknown";
    }
    return key_table[code].name;
}

static uint8_t modifier_for_key(enum key_code code)
{
    switch (code) {
    case KEY_LEFT_SHIFT:
    case KEY_RIGHT_SHIFT:
        return KEY_MODIFIER_SHIFT;
    case KEY_LEFT_CONTROL:
    case KEY_RIGHT_CONTROL:
        return KEY_MODIFIER_CONTROL;
    case KEY_LEFT_ALT:
    case KEY_RIGHT_ALT:
        return KEY_MODIFIER_ALT;
    case KEY_LEFT_SUPER:
    case KEY_RIGHT_SUPER:
        return KEY_MODIFIER_SUPER;
    default:
        return 0;
    }
}

static void update_modifiers(enum key_code code, bool pressed)
{
    /* Caps lock is a toggle: it changes on press and ignores release. */
    if (code == KEY_CAPS_LOCK) {
        if (pressed) {
            modifiers ^= KEY_MODIFIER_CAPS_LOCK;
        }
        return;
    }
    uint8_t modifier = modifier_for_key(code);
    if (pressed) {
        modifiers |= modifier;
    } else {
        modifiers &= (uint8_t)~modifier;
    }
}

static bool is_letter(char character)
{
    return character >= 'a' && character <= 'z';
}

static char character_for_key(enum key_code code)
{
    const struct key_info *info = &key_table[code];
    bool shift = (modifiers & KEY_MODIFIER_SHIFT) != 0;

    /* Caps lock affects letters only, and shift undoes it. */
    if (is_letter(info->normal) && (modifiers & KEY_MODIFIER_CAPS_LOCK) != 0) {
        shift = !shift;
    }
    return shift ? info->shifted : info->normal;
}

void keyboard_report(enum key_code code, bool pressed)
{
    if (code == KEY_NONE || code >= KEY_CODE_COUNT) {
        return;
    }
    arch_irq_state_t irq_state = spinlock_acquire(&lock);

    update_modifiers(code, pressed);
    /* When the queue is full the new event is dropped: losing the most
     * recent key is less confusing than losing one typed long ago. */
    if (queue_count < QUEUE_CAPACITY) {
        size_t tail = (queue_head + queue_count) % QUEUE_CAPACITY;
        queue[tail] = (struct sd_key_event){
            .code = (uint16_t)code,
            .pressed = pressed ? 1 : 0,
            .modifiers = modifiers,
            .character = character_for_key(code),
        };
        queue_count++;
        wait_queue_wake_one(&readers);
    }

    spinlock_release(&lock, irq_state);

    /* Outside the lock: the listener may take locks of its own. */
    if (event_listener != NULL) {
        event_listener();
    }
}

void keyboard_set_listener(void (*listener)(void))
{
    event_listener = listener;
}

bool keyboard_has_event(void)
{
    arch_irq_state_t irq_state = spinlock_acquire(&lock);
    bool available = queue_count > 0;
    spinlock_release(&lock, irq_state);
    return available;
}

static void pop_event_locked(struct sd_key_event *event)
{
    *event = queue[queue_head];
    queue_head = (queue_head + 1) % QUEUE_CAPACITY;
    queue_count--;
}

void keyboard_wait_event(struct sd_key_event *event)
{
    arch_irq_state_t irq_state = spinlock_acquire(&lock);
    while (queue_count == 0) {
        wait_queue_wait(&readers, &lock);
    }
    pop_event_locked(event);
    spinlock_release(&lock, irq_state);
}

bool keyboard_read_event(struct sd_key_event *event)
{
    arch_irq_state_t irq_state = spinlock_acquire(&lock);

    bool available = queue_count > 0;
    if (available) {
        pop_event_locked(event);
    }

    spinlock_release(&lock, irq_state);
    return available;
}

void keyboard_log_event(const struct sd_key_event *event)
{
    const char *action = event->pressed ? "pressed" : "released";
    const char *name = keyboard_key_name((enum key_code)event->code);

    /* Only show the character if it is a visible one. */
    if (event->character > ' ' && event->character <= '~') {
        log_info("key: %s %s, character '%c'", name, action, event->character);
    } else {
        log_info("key: %s %s", name, action);
    }
}
