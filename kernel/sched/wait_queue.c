#include <sched/wait_queue.h>

#include "sched_internal.h"

void wait_queue_init(struct wait_queue *queue)
{
    list_init(&queue->waiters);
}

/* Shared by the plain and the timed wait; `timeout_ms` < 0 means forever. */
static bool wait_common(struct wait_queue *queue, struct spinlock *lock, int64_t timeout_ms)
{
    /* Interrupts are already off (the caller holds `lock`), so the raw
     * variants are correct here. Lock order: caller's lock, then sched_lock;
     * the wake functions use the same order. */
    spinlock_acquire_raw(&sched_lock);

    struct thread *self = sched_current_locked();
    self->state = THREAD_BLOCKED;
    self->timed_out = false;
    list_push_back(&queue->waiters, &self->queue_link);
    if (timeout_ms >= 0) {
        /* In both queues at once: whichever happens first, the wake-up or
         * the deadline, removes the thread from the other one. */
        sched_sleep_queue_insert_locked(self, (uint64_t)timeout_ms);
    }

    /* We are on the queue, so from here a wake-up will find us. Only now
     * may the caller's lock be released. */
    spinlock_release_raw(lock);
    sched_switch_locked();
    bool woken = !self->timed_out;
    spinlock_release_raw(&sched_lock);

    spinlock_acquire_raw(lock);
    return woken;
}

void wait_queue_wait(struct wait_queue *queue, struct spinlock *lock)
{
    (void)wait_common(queue, lock, -1);
}

bool wait_queue_wait_timeout(struct wait_queue *queue, struct spinlock *lock, uint64_t timeout_ms)
{
    /* Clamp so the value fits the signed parameter; about 292 million years. */
    int64_t bounded = timeout_ms > (uint64_t)INT64_MAX ? INT64_MAX : (int64_t)timeout_ms;
    return wait_common(queue, lock, bounded);
}

static void wake_first_locked(struct wait_queue *queue)
{
    struct thread *thread = container_of(queue->waiters.next, struct thread, queue_link);
    list_remove(&thread->queue_link);
    sched_make_ready_locked(thread);
}

void wait_queue_wake_one(struct wait_queue *queue)
{
    arch_irq_state_t irq_state = spinlock_acquire(&sched_lock);
    if (!list_is_empty(&queue->waiters)) {
        wake_first_locked(queue);
    }
    spinlock_release(&sched_lock, irq_state);
}

void wait_queue_wake_all(struct wait_queue *queue)
{
    arch_irq_state_t irq_state = spinlock_acquire(&sched_lock);
    while (!list_is_empty(&queue->waiters)) {
        wake_first_locked(queue);
    }
    spinlock_release(&sched_lock, irq_state);
}
