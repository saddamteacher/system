#ifndef SCHED_SCHED_INTERNAL_H
#define SCHED_SCHED_INTERNAL_H

#include <core/list.h>
#include <core/spinlock.h>
#include <sched/thread.h>
#include <sched/wait_queue.h>

/* Shared between the files of kernel/sched only. */

enum thread_state {
    THREAD_READY,    /* in the run queue */
    THREAD_RUNNING,  /* on the CPU */
    THREAD_BLOCKED,  /* in a wait queue */
    THREAD_SLEEPING, /* in the sleep queue until wake_tick */
    THREAD_ZOMBIE,   /* ended; waiting to be joined or reaped */
};

#define THREAD_NAME_LENGTH 24

struct thread {
    uint64_t id;
    char name[THREAD_NAME_LENGTH];
    enum thread_state state;

    void *context;      /* saved stack pointer while not running */
    void *stack_top;    /* NULL for the idle thread: its stack is not ours */
    thread_entry_fn entry;
    void *argument;

    /* Set only for threads that run user code. */
    struct arch_address_space *space; /* NULL: kernel thread, any space will do */
    void *fpu_state;                  /* saved floating point registers */
    void *owner;                      /* the process, opaque to the scheduler */

    /* A thread is in at most one of the run, wait and reap queues at a
     * time, so one link serves for all of them. */
    struct list_node queue_link;
    /* The sleep queue has its own link: a thread waiting with a timeout is
     * in a wait queue and in the sleep queue at once. */
    struct list_node sleep_link;
    uint64_t wake_tick;
    bool timed_out; /* the last timed wait ended by timeout, not wake-up */
    unsigned slice_ticks_left;

    bool detached;
    struct wait_queue exit_waiters; /* threads blocked in thread_join() */
};

/* The scheduler lock protects all thread states and all queues of threads.
 * It is always taken with interrupts disabled. */
extern struct spinlock sched_lock;

/* All of these require sched_lock to be held. */
struct thread *sched_current_locked(void);
void sched_make_ready_locked(struct thread *thread);
/* Gives up the CPU. The caller must first have set the current thread's
 * state and queued it wherever it should wait (nowhere if it stays
 * RUNNING: then it is put back in the run queue). */
void sched_switch_locked(void);
/* Arranges for `thread` to be made ready after `milliseconds`. */
void sched_sleep_queue_insert_locked(struct thread *thread, uint64_t milliseconds);

#endif
