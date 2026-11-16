# 04 - Threads and scheduling

The kernel can now do several things at once: independent threads, each with
its own stack, sharing one CPU under a preemptive round-robin scheduler.

## Context switch

`arch_context_switch(&save, load)` (`arch/x86_64/context.asm`) is an ordinary
function call from the compiler's point of view, so the caller-saved
registers are already dead. It pushes the six callee-saved registers (`rbx`,
`rbp`, `r12`-`r15`) on the current stack, stores `rsp` through `save`, loads
`rsp` from `load`, pops the same six registers and returns. The `ret` pops
whatever return address the *other* thread left on its stack: that is the
whole trick.

A thread is therefore represented by one saved stack pointer; its state
lives on its own stack.

A new thread has never called `arch_context_switch`, so
`arch_context_create` fabricates the stack a suspended thread would have:
six zeroed registers and the address of the entry function where the return
address belongs, plus one dummy slot so the stack is aligned as after a
`call`.

## Threads

`struct thread` (`kernel/sched/sched_internal.h`): id, name, state, saved
context, stack, entry point and argument, one list link, wake-up tick,
remaining time slice.

States: `READY` (in the run queue), `RUNNING`, `BLOCKED` (in a wait queue),
`SLEEPING` (in the sleep queue), `ZOMBIE` (ended).

A thread is in at most one queue at a time, so a single list link serves the
run queue, the sleep queue, wait queues and the reap queue.

### Boot and the idle thread

`sched_init` wraps the code that is already running (the boot context, on its
guarded stack) in a `struct thread`: it becomes the **idle thread**. It is
never in the run queue; it is what runs when the queue is empty. It must
never block, so `kmain` starts a separate `main` thread for everything else
and then enters `sched_idle_loop`.

### Ending a thread

A thread cannot free the stack it is running on. `thread_exit` marks it
`ZOMBIE` and switches away for the last time; the memory is released later by
someone else:

- `thread_join(t)` waits for `t` to end, then frees it;
- `thread_detach(t)` hands that job to the idle thread (the reap queue).

Each thread must be joined or detached exactly once.

## Scheduler

`sched_switch_locked` puts the current thread at the back of the run queue
if it is still runnable, takes the front of the queue (or the idle thread)
and switches.

**Preemption.** The tick handler `sched_tick` decrements the running
thread's slice (10 ticks = 10 ms). When it reaches zero it calls
`sched_switch_locked` *from inside the interrupt handler*. The interrupted
thread's trap frame stays on its stack; when the thread is scheduled again it
returns out of the handler and `iretq` resumes it exactly where it was. This
is why the end-of-interrupt is sent before the handler runs (Phase 3).

**Sleeping.** `thread_sleep_ms` computes a wake-up tick and inserts the
thread into the sleep queue, which is kept sorted. The tick handler only has
to look at the front. One extra tick is added because the current tick is
already partly over and the guarantee is "at least".

### The lock across a switch

All thread states and queues are protected by `sched_lock`, taken with
interrupts disabled. A thread takes the lock, switches, and the thread that
runs next releases it. So the code after `arch_context_switch`, and
`thread_start` for a brand-new thread, begin with the lock held by their
predecessor. Each thread restores the interrupt state *it* saved, which is
why a thread preempted in an interrupt handler and a thread that yielded
voluntarily can resume each other.

## Wait queues

A wait queue is where threads sleep until a condition becomes true. The
pattern:

```c
irq = spinlock_acquire(&lock);
while (!condition)
    wait_queue_wait(&queue, &lock);
/* condition holds, lock held */
spinlock_release(&lock, irq);
```

`wait_queue_wait` enqueues the thread under `sched_lock` *before* releasing
the caller's lock, so a wake-up issued after the condition changes cannot be
missed. The `while` is required: by the time the woken thread runs, another
thread may have consumed what it was woken for.

`wait_queue_wake_one/all` are safe from interrupt context; the keyboard
interrupt uses them to wake `keyboard_wait_event`.

## Mutex

A sleeping lock built from a spinlock, an owner pointer and a wait queue.
Locking sleeps while there is an owner; unlocking clears the owner and wakes
one waiter. Locking a mutex twice from the same thread, or unlocking one you
do not own, panics.

Spinlock or mutex: a spinlock for short sections and anything touched by
interrupt handlers; a mutex when the holder may sleep.

## Checks

| Check | What it proves |
|---|---|
| `threads` | three threads that never yield each get about a third of the CPU, in about 10 turns each over 300 ms: preemption works |
| `sleep` | a 50 ms sleep takes 50-60 ms; three sleepers wake in deadline order |
| `mutex` | four threads doing read / yield / write on a counter lose updates without a mutex (500 of 2000 arrive) and none with it |
| `thread-churn` | 200 threads created and released, half joined and half detached, leave heap, frames and thread count unchanged |

## Known limits

- One CPU. The structure (a scheduler lock, per-thread stacks) is SMP-shaped,
  but there is one run queue and one `current`.
- No priorities: every thread is equal.
- A thread woken from an interrupt runs at the next tick or when the current
  thread blocks, so up to 1 ms later.
- The mutex is not fair: a running thread can take it before a waiter that
  was just woken.
