/*
 * One wait queue serves all pollers: any change wakes all of them and each
 * checks its own files again. That costs spurious wake-ups, but it is
 * simple and correct, and the number of pollers is small.
 *
 * Why poll_lock exists: a poller looks at its files and, finding nothing,
 * goes to sleep. If a file became ready between the look and the sleep, the
 * notification would arrive while nobody is sleeping and be lost. So the
 * poller holds poll_lock from the look until it is safely on the queue
 * (wait_queue_wait releases it only then), and poll_notify() takes the same
 * lock, which makes it wait for that moment.
 *
 * Lock order: poll_lock, then a file's own lock (inside its poll
 * operation), then the scheduler lock.
 */
#include <fs/poll.h>

#include <core/spinlock.h>
#include <core/time.h>
#include <fs/vfs.h>
#include <sched/wait_queue.h>

static struct spinlock poll_lock = SPINLOCK_INIT;
static struct wait_queue pollers = { .waiters = { &pollers.waiters, &pollers.waiters } };

static int scan(struct poll_entry *entries, size_t count)
{
    int ready = 0;
    for (size_t i = 0; i < count; i++) {
        entries[i].revents = 0;
        if (entries[i].file == NULL) {
            continue;
        }
        unsigned state = vfs_poll(entries[i].file);
        /* A hang-up is reported whether or not it was asked for: a caller
         * waiting for data from a dead peer would otherwise wait forever. */
        entries[i].revents = (uint16_t)(state & (entries[i].events | SD_POLL_HUP));
        if (entries[i].revents != 0) {
            ready++;
        }
    }
    return ready;
}

int poll_wait(struct poll_entry *entries, size_t count, int64_t timeout_ms)
{
    uint64_t deadline = time_now_ms() + (timeout_ms > 0 ? (uint64_t)timeout_ms : 0);

    arch_irq_state_t irq_state = spinlock_acquire(&poll_lock);
    int ready;
    for (;;) {
        ready = scan(entries, count);
        if (ready > 0 || timeout_ms == 0) {
            break;
        }
        if (timeout_ms < 0) {
            wait_queue_wait(&pollers, &poll_lock);
            continue;
        }
        uint64_t now = time_now_ms();
        if (now >= deadline) {
            break;
        }
        wait_queue_wait_timeout(&pollers, &poll_lock, deadline - now);
    }
    spinlock_release(&poll_lock, irq_state);
    return ready;
}

void poll_notify(void)
{
    arch_irq_state_t irq_state = spinlock_acquire(&poll_lock);
    wait_queue_wake_all(&pollers);
    spinlock_release(&poll_lock, irq_state);
}
