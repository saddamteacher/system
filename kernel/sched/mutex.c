#include <sched/mutex.h>

#include <stddef.h>

#include <core/panic.h>
#include <sched/thread.h>

void mutex_init(struct mutex *mutex)
{
    mutex->lock = (struct spinlock)SPINLOCK_INIT;
    mutex->owner = NULL;
    wait_queue_init(&mutex->waiters);
}

void mutex_lock(struct mutex *mutex)
{
    struct thread *self = thread_current();
    arch_irq_state_t irq_state = spinlock_acquire(&mutex->lock);

    if (mutex->owner == self) {
        panic("mutex: thread '%s' locked a mutex it already holds", thread_name(self));
    }
    while (mutex->owner != NULL) {
        wait_queue_wait(&mutex->waiters, &mutex->lock);
    }
    mutex->owner = self;

    spinlock_release(&mutex->lock, irq_state);
}

void mutex_unlock(struct mutex *mutex)
{
    struct thread *self = thread_current();
    arch_irq_state_t irq_state = spinlock_acquire(&mutex->lock);

    if (mutex->owner != self) {
        panic("mutex: thread '%s' unlocked a mutex it does not hold", thread_name(self));
    }
    mutex->owner = NULL;
    /* One waiter is enough: only one thread can take the mutex anyway. */
    wait_queue_wake_one(&mutex->waiters);

    spinlock_release(&mutex->lock, irq_state);
}
