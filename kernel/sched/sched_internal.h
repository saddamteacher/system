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

    /* A thread is in at most one queue at a time (run, sleep, wait or reap
     * queue), so one link serves for all of them. */
    struct list_node queue_link;
    uint64_t wake_tick;
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

#endif
