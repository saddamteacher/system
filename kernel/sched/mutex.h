#ifndef SCHED_MUTEX_H
#define SCHED_MUTEX_H

#include <sched/wait_queue.h>

/*
 * Mutual exclusion lock for threads. Unlike a spinlock, a thread that finds
 * the mutex taken goes to sleep instead of spinning, so a mutex may be held
 * for a long time and across blocking calls.
 *
 * Not usable from interrupt context (an interrupt handler cannot sleep).
 * Not recursive: locking a mutex you already hold is a bug and panics.
 */
struct thread;

struct mutex {
    struct spinlock lock; /* protects owner */
    struct thread *owner; /* NULL when unlocked */
    struct wait_queue waiters;
};

void mutex_init(struct mutex *mutex);
void mutex_lock(struct mutex *mutex);
/* Only the thread that locked the mutex may unlock it. */
void mutex_unlock(struct mutex *mutex);

#endif
