/*
 * Device-independent half of mouse input: the event queue.
 *
 * State (protected by `lock`): the queue. The writer is the mouse
 * interrupt, the reader a thread.
 */
#include <drivers/mouse/mouse.h>

#include <stddef.h>

#include <core/spinlock.h>

#define QUEUE_CAPACITY 64

static struct spinlock lock = SPINLOCK_INIT;
static struct sd_mouse_event queue[QUEUE_CAPACITY];
static size_t queue_head;
static size_t queue_count;
/* Written once during boot. */
static void (*event_listener)(void);

static int16_t clamp_to_int16(int value)
{
    if (value > INT16_MAX) {
        return INT16_MAX;
    }
    return value < INT16_MIN ? INT16_MIN : (int16_t)value;
}

void mouse_report(int dx, int dy, int wheel, unsigned buttons)
{
    arch_irq_state_t irq_state = spinlock_acquire(&lock);

    if (queue_count == QUEUE_CAPACITY) {
        /* The reader is not keeping up. Dropping motion would make the
         * pointer fall behind the hand, so fold it into the newest event:
         * the position stays right, only intermediate points are lost. */
        struct sd_mouse_event *last = &queue[(queue_head + queue_count - 1) % QUEUE_CAPACITY];
        last->dx = clamp_to_int16(last->dx + dx);
        last->dy = clamp_to_int16(last->dy + dy);
        last->wheel = (int8_t)(last->wheel + wheel);
        last->buttons = (uint8_t)buttons;
    } else {
        queue[(queue_head + queue_count) % QUEUE_CAPACITY] = (struct sd_mouse_event){
            .dx = clamp_to_int16(dx),
            .dy = clamp_to_int16(dy),
            .wheel = (int8_t)wheel,
            .buttons = (uint8_t)buttons,
        };
        queue_count++;
    }

    spinlock_release(&lock, irq_state);

    if (event_listener != NULL) {
        event_listener();
    }
}

bool mouse_read_event(struct sd_mouse_event *event)
{
    arch_irq_state_t irq_state = spinlock_acquire(&lock);
    bool available = queue_count > 0;
    if (available) {
        *event = queue[queue_head];
        queue_head = (queue_head + 1) % QUEUE_CAPACITY;
        queue_count--;
    }
    spinlock_release(&lock, irq_state);
    return available;
}

bool mouse_has_event(void)
{
    arch_irq_state_t irq_state = spinlock_acquire(&lock);
    bool available = queue_count > 0;
    spinlock_release(&lock, irq_state);
    return available;
}

void mouse_set_listener(void (*listener)(void))
{
    event_listener = listener;
}
