/*
 * Threads and the round-robin scheduler.
 *
 * State, all protected by sched_lock:
 *   current      the thread on the CPU
 *   run_queue    READY threads, next to run at the front
 *   sleep_queue  SLEEPING threads, soonest wake-up at the front
 *   reap_queue   detached ZOMBIE threads waiting to be freed
 *   every struct thread, the id counter and the statistics
 *
 * The lock across a switch: a thread takes sched_lock, calls
 * sched_switch_locked(), and the thread that runs next releases it. So the
 * code after every arch_context_switch(), and thread_start() for a thread
 * running for the first time, begins with the lock held by its predecessor.
 */
#include <sched/thread.h>

#include <arch/context.h>
#include <arch/cpu.h>
#include <arch/fpu.h>
#include <arch/mmu.h>
#include <arch/irq.h>
#include <core/log.h>
#include <core/panic.h>
#include <core/string.h>
#include <core/time.h>
#include <mm/heap.h>
#include <mm/vmm.h>

#include "sched_internal.h"

#define THREAD_STACK_PAGES 8
/* Length of a time slice. Shorter is more responsive, longer wastes less
 * time switching; 10 ms is a common middle ground. */
#define TIME_SLICE_TICKS 10

struct spinlock sched_lock = SPINLOCK_INIT;

static struct thread *current;
static struct thread idle_thread;
static struct list_node run_queue;
static struct list_node sleep_queue;
static struct list_node reap_queue;
static uint64_t next_thread_id;
static struct sched_stats stats;

static struct thread *thread_from_link(struct list_node *link)
{
    return container_of(link, struct thread, queue_link);
}

struct thread *sched_current_locked(void)
{
    return current;
}

void sched_make_ready_locked(struct thread *thread)
{
    thread->state = THREAD_READY;
    list_push_back(&run_queue, &thread->queue_link);
}

static struct thread *pick_next_locked(void)
{
    if (list_is_empty(&run_queue)) {
        return &idle_thread;
    }
    struct thread *next = thread_from_link(run_queue.next);
    list_remove(&next->queue_link);
    return next;
}

/* Everything about a thread that lives outside its kernel stack. */
static void switch_user_state_locked(struct thread *previous, struct thread *next)
{
    if (previous->fpu_state != NULL) {
        arch_fpu_save(previous->fpu_state);
    }
    /* A kernel thread only touches the kernel half, which every address
     * space has; still, give it the kernel's own, so that no user space
     * stays loaded after its process may have been destroyed. */
    arch_mmu_space_switch(next->space != NULL ? next->space : arch_mmu_kernel_space());
    if (next->stack_top != NULL) {
        arch_set_kernel_stack(next->stack_top);
    }
    if (next->fpu_state != NULL) {
        arch_fpu_restore(next->fpu_state);
    }
}

void sched_switch_locked(void)
{
    struct thread *previous = current;

    /* A thread that is still RUNNING was preempted or yielded: it goes to
     * the back of the queue. The idle thread is never queued; it is what
     * runs when the queue is empty. */
    if (previous->state == THREAD_RUNNING) {
        if (previous == &idle_thread) {
            previous->state = THREAD_READY;
        } else {
            sched_make_ready_locked(previous);
        }
    }
    struct thread *next = pick_next_locked();
    next->state = THREAD_RUNNING;
    next->slice_ticks_left = TIME_SLICE_TICKS;
    if (next == previous) {
        return;
    }

    current = next;
    stats.context_switches++;
    switch_user_state_locked(previous, next);
    arch_context_switch(&previous->context, next->context);
    /* Running again, possibly much later. `current` is `previous` again. */
}

/* Moves threads whose sleep is over to the run queue. */
static void wake_sleepers_locked(uint64_t now)
{
    while (!list_is_empty(&sleep_queue)) {
        struct thread *thread = thread_from_link(sleep_queue.next);
        if (thread->wake_tick > now) {
            break; /* sorted: nobody further back is due either */
        }
        list_remove(&thread->queue_link);
        sched_make_ready_locked(thread);
    }
}

/* Timer tick, in interrupt context. This is where preemption happens. */
static void sched_tick(void)
{
    spinlock_acquire_raw(&sched_lock);

    wake_sleepers_locked(time_ticks());

    bool slice_used_up = current->slice_ticks_left <= 1;
    if (!slice_used_up) {
        current->slice_ticks_left--;
    }
    /* The idle thread gives way as soon as anything else can run. */
    bool idle_can_yield = current == &idle_thread && !list_is_empty(&run_queue);

    if (slice_used_up || idle_can_yield) {
        /* We switch away in the middle of the interrupt handler. The
         * interrupted state stays on this thread's stack; when the thread
         * is scheduled again it returns from here, out of the handler, and
         * `iretq` resumes the interrupted code. */
        sched_switch_locked();
    }

    spinlock_release_raw(&sched_lock);
}

/* First code of every new thread, entered from arch_context_switch(). */
static void thread_start(void)
{
    /* The thread that switched to us still holds the lock for us. */
    spinlock_release_raw(&sched_lock);
    arch_irq_enable();

    struct thread *self = thread_current();
    self->entry(self->argument);
    thread_exit();
}

void sched_init(void)
{
    list_init(&run_queue);
    list_init(&sleep_queue);
    list_init(&reap_queue);

    /* The code calling us is already running, on a stack that already
     * exists; all it lacks to be a thread is the bookkeeping. */
    idle_thread = (struct thread){ .id = next_thread_id++, .state = THREAD_RUNNING };
    memcpy(idle_thread.name, "idle", sizeof("idle"));
    list_init(&idle_thread.queue_link);
    wait_queue_init(&idle_thread.exit_waiters);
    current = &idle_thread;
    stats.live_threads = 1;

    time_set_tick_hook(sched_tick);
    log_info("sched: round-robin, %u ms time slice", TIME_SLICE_TICKS * TIME_MS_PER_TICK);
}

static void copy_name(char *destination, const char *source)
{
    size_t length = strlen(source);
    if (length >= THREAD_NAME_LENGTH) {
        length = THREAD_NAME_LENGTH - 1;
    }
    memcpy(destination, source, length);
    destination[length] = '\0';
}

struct thread *thread_create(const char *name, thread_entry_fn entry, void *argument)
{
    return thread_create_user(name, entry, argument, NULL, NULL);
}

struct thread *thread_create_user(const char *name, thread_entry_fn entry, void *argument,
                                  struct arch_address_space *space, void *owner)
{
    struct thread *thread = kzalloc(sizeof(*thread));
    if (thread == NULL) {
        return NULL;
    }
    thread->stack_top = vmm_alloc_kernel_stack(THREAD_STACK_PAGES);
    if (space != NULL) {
        /* kmalloc aligns to 16 bytes, which is what the state needs. */
        thread->fpu_state = kmalloc(arch_fpu_state_size());
    }
    if (thread->stack_top == NULL || (space != NULL && thread->fpu_state == NULL)) {
        if (thread->stack_top != NULL) {
            vmm_free_kernel_stack(thread->stack_top, THREAD_STACK_PAGES);
        }
        kfree(thread->fpu_state);
        kfree(thread);
        return NULL;
    }
    if (thread->fpu_state != NULL) {
        arch_fpu_state_init(thread->fpu_state);
    }
    thread->space = space;
    thread->owner = owner;
    copy_name(thread->name, name);
    thread->entry = entry;
    thread->argument = argument;
    thread->context = arch_context_create(thread->stack_top, thread_start);
    list_init(&thread->queue_link);
    wait_queue_init(&thread->exit_waiters);

    arch_irq_state_t irq_state = spinlock_acquire(&sched_lock);
    thread->id = next_thread_id++;
    stats.live_threads++;
    sched_make_ready_locked(thread);
    spinlock_release(&sched_lock, irq_state);
    return thread;
}

/* Frees a thread that has ended and is no longer on its own stack. */
static void thread_release(struct thread *thread)
{
    vmm_free_kernel_stack(thread->stack_top, THREAD_STACK_PAGES);
    kfree(thread->fpu_state);
    kfree(thread);

    arch_irq_state_t irq_state = spinlock_acquire(&sched_lock);
    stats.live_threads--;
    spinlock_release(&sched_lock, irq_state);
}

_Noreturn void thread_exit(void)
{
    (void)spinlock_acquire(&sched_lock);

    struct thread *self = current;
    if (self == &idle_thread) {
        panic("sched: the idle thread tried to exit");
    }
    self->state = THREAD_ZOMBIE;

    /* A thread cannot free the stack it is standing on. It only marks
     * itself as ended; whoever joins it, or the idle thread for a detached
     * one, frees it after we have switched away for the last time. */
    while (!list_is_empty(&self->exit_waiters.waiters)) {
        struct thread *waiter = thread_from_link(self->exit_waiters.waiters.next);
        list_remove(&waiter->queue_link);
        sched_make_ready_locked(waiter);
    }
    if (self->detached) {
        list_push_back(&reap_queue, &self->queue_link);
    }

    sched_switch_locked();
    panic("sched: a zombie thread was scheduled");
}

void thread_join(struct thread *thread)
{
    arch_irq_state_t irq_state = spinlock_acquire(&sched_lock);

    if (thread->detached || thread == current) {
        panic("sched: invalid join of thread '%s'", thread->name);
    }
    while (thread->state != THREAD_ZOMBIE) {
        current->state = THREAD_BLOCKED;
        list_push_back(&thread->exit_waiters.waiters, &current->queue_link);
        sched_switch_locked();
    }

    spinlock_release(&sched_lock, irq_state);
    /* The zombie switched away under the lock we just held, so it is
     * certainly off its stack. */
    thread_release(thread);
}

void thread_detach(struct thread *thread)
{
    arch_irq_state_t irq_state = spinlock_acquire(&sched_lock);

    thread->detached = true;
    /* It may have ended already; then nobody queued it for reaping. */
    if (thread->state == THREAD_ZOMBIE) {
        list_push_back(&reap_queue, &thread->queue_link);
    }

    spinlock_release(&sched_lock, irq_state);
}

void thread_yield(void)
{
    arch_irq_state_t irq_state = spinlock_acquire(&sched_lock);
    sched_switch_locked();
    spinlock_release(&sched_lock, irq_state);
}

/* Keeps the sleep queue sorted by wake-up time, so the tick handler only
 * ever has to look at the front. */
static void sleep_queue_insert_locked(struct thread *thread)
{
    struct list_node *position = sleep_queue.next;
    while (position != &sleep_queue && thread_from_link(position)->wake_tick <= thread->wake_tick) {
        position = position->next;
    }
    list_insert_between(&thread->queue_link, position->prev, position);
}

void thread_sleep_ms(uint64_t milliseconds)
{
    arch_irq_state_t irq_state = spinlock_acquire(&sched_lock);

    if (current == &idle_thread) {
        panic("sched: the idle thread tried to sleep");
    }
    /* One extra tick: the current tick is already partly over, and the
     * promise is "at least" the requested time. */
    uint64_t ticks = (milliseconds + TIME_MS_PER_TICK - 1) / TIME_MS_PER_TICK + 1;
    current->wake_tick = time_ticks() + ticks;
    current->state = THREAD_SLEEPING;
    sleep_queue_insert_locked(current);
    sched_switch_locked();

    spinlock_release(&sched_lock, irq_state);
}

/* Frees detached threads that have ended. Runs in the idle thread. */
static void reap_zombies(void)
{
    for (;;) {
        arch_irq_state_t irq_state = spinlock_acquire(&sched_lock);
        struct thread *zombie = NULL;
        if (!list_is_empty(&reap_queue)) {
            zombie = thread_from_link(reap_queue.next);
            list_remove(&zombie->queue_link);
        }
        spinlock_release(&sched_lock, irq_state);

        if (zombie == NULL) {
            return;
        }
        thread_release(zombie);
    }
}

_Noreturn void sched_idle_loop(void)
{
    for (;;) {
        reap_zombies();
        /* Sleep until an interrupt; if it made a thread runnable, let it
         * run right away instead of waiting for the next tick. */
        arch_cpu_idle();
        thread_yield();
    }
}

struct thread *thread_current(void)
{
    arch_irq_state_t irq_state = spinlock_acquire(&sched_lock);
    struct thread *thread = current;
    spinlock_release(&sched_lock, irq_state);
    return thread;
}

void *thread_owner(const struct thread *thread)
{
    return thread->owner;
}

void thread_leave_address_space(void)
{
    arch_irq_state_t irq_state = spinlock_acquire(&sched_lock);
    current->space = NULL;
    arch_mmu_space_switch(arch_mmu_kernel_space());
    spinlock_release(&sched_lock, irq_state);
}

const char *thread_name(const struct thread *thread)
{
    return thread->name;
}

uint64_t thread_id(const struct thread *thread)
{
    return thread->id;
}

struct sched_stats sched_stats(void)
{
    arch_irq_state_t irq_state = spinlock_acquire(&sched_lock);
    struct sched_stats copy = stats;
    spinlock_release(&sched_lock, irq_state);
    return copy;
}
