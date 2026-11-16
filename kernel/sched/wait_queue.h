#ifndef SCHED_WAIT_QUEUE_H
#define SCHED_WAIT_QUEUE_H

#include <stdbool.h>
#include <stdint.h>

#include <core/list.h>
#include <core/spinlock.h>

/*
 * A wait queue is where threads sleep until something they are waiting for
 * happens. It is the building block for every blocking operation.
 *
 * Usage, with a spinlock that protects the awaited condition:
 *
 *     irq = spinlock_acquire(&lock);
 *     while (!condition)
 *         wait_queue_wait(&queue, &lock);
 *     ... condition holds, lock held ...
 *     spinlock_release(&lock, irq);
 *
 * and on the other side, after making the condition true under the same
 * lock: wait_queue_wake_one() or wait_queue_wake_all().
 *
 * The loop is required: by the time a woken thread runs again, another
 * thread may already have consumed what it was woken for.
 */
struct wait_queue {
    struct list_node waiters;
};

void wait_queue_init(struct wait_queue *queue);

/*
 * Atomically releases `lock` and puts the calling thread to sleep on the
 * queue; re-acquires `lock` before returning. "Atomically" is the point: a
 * wake-up issued after the lock is released cannot be missed.
 * `lock` must be held with interrupts disabled (as spinlock_acquire does).
 */
void wait_queue_wait(struct wait_queue *queue, struct spinlock *lock);

/*
 * Like wait_queue_wait(), but gives up after `timeout_ms`. Returns true if
 * the thread was woken, false if the time ran out. Either way `lock` is
 * held again on return and the condition must be re-checked.
 */
bool wait_queue_wait_timeout(struct wait_queue *queue, struct spinlock *lock, uint64_t timeout_ms);

/* Make the longest-waiting thread, or all waiting threads, runnable. Safe
 * from interrupt context. */
void wait_queue_wake_one(struct wait_queue *queue);
void wait_queue_wake_all(struct wait_queue *queue);

#endif
