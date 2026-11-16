#include <sched/wait_queue.h>

#include "sched_internal.h"

void wait_queue_init(struct wait_queue *queue)
{
    list_init(&queue->waiters);
}

void wait_queue_wait(struct wait_queue *queue, struct spinlock *lock)
{
    /* Interrupts are already off (the caller holds `lock`), so the raw
     * variants are correct here. Lock order: caller's lock, then sched_lock;
     * the wake functions use the same order. */
    spinlock_acquire_raw(&sched_lock);

    struct thread *self = sched_current_locked();
    self->state = THREAD_BLOCKED;
    list_push_back(&queue->waiters, &self->queue_link);

    /* We are on the queue, so from here a wake-up will find us. Only now
     * may the caller's lock be released. */
    spinlock_release_raw(lock);
    sched_switch_locked();
    spinlock_release_raw(&sched_lock);

    spinlock_acquire_raw(lock);
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
