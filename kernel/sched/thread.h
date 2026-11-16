#ifndef SCHED_THREAD_H
#define SCHED_THREAD_H

#include <stdbool.h>
#include <stdint.h>

/*
 * Kernel threads and the scheduler.
 *
 * Scheduling is preemptive round-robin: every runnable thread gets a time
 * slice in turn, and the timer interrupt takes the CPU away when the slice
 * is used up. A thread also gives up the CPU when it sleeps, blocks or
 * yields.
 */

struct thread;

typedef void (*thread_entry_fn)(void *argument);

/* Turns the calling context into the idle thread and starts preemption.
 * Requires the heap and the tick. Call once. */
void sched_init(void);

/* Body of the idle thread: runs when nothing else can. Never returns. */
_Noreturn void sched_idle_loop(void);

/*
 * Creates a thread that runs entry(argument) and makes it runnable. Returns
 * NULL if out of memory. The thread ends when `entry` returns or it calls
 * thread_exit().
 *
 * Every thread must be either joined (thread_join) or detached
 * (thread_detach), exactly once; otherwise its memory is never released.
 */
struct thread *thread_create(const char *name, thread_entry_fn entry, void *argument);

/* Waits until `thread` has ended, then releases it. The pointer is invalid
 * afterwards. */
void thread_join(struct thread *thread);

/* Declares that nobody will join `thread`: it is released automatically
 * when it ends. The pointer must not be used afterwards. */
void thread_detach(struct thread *thread);

_Noreturn void thread_exit(void);

/* Lets other runnable threads run; returns when it is this thread's turn. */
void thread_yield(void);

/* Blocks the calling thread for at least `milliseconds`. */
void thread_sleep_ms(uint64_t milliseconds);

struct thread *thread_current(void);
const char *thread_name(const struct thread *thread);
uint64_t thread_id(const struct thread *thread);

struct sched_stats {
    uint64_t context_switches;
    uint64_t live_threads; /* created and not yet released, idle included */
};

struct sched_stats sched_stats(void);

#endif
