#ifndef CORE_SPINLOCK_H
#define CORE_SPINLOCK_H

#include <stdatomic.h>

#include <arch/cpu.h>

/*
 * Spinlock that also disables interrupts while held.
 *
 * Why both: if an interrupt handler tried to take a lock that the code it
 * interrupted already holds, it would spin forever, because that code cannot
 * run again until the handler returns. Disabling interrupts for the duration
 * of the critical section rules this out.
 *
 * Critical sections must be short and must not sleep.
 */
struct spinlock {
    atomic_flag locked;
};

#define SPINLOCK_INIT { ATOMIC_FLAG_INIT }

static inline arch_irq_state_t spinlock_acquire(struct spinlock *lock)
{
    arch_irq_state_t irq_state = arch_irq_save();
    /* test_and_set atomically writes "locked" and returns the old value;
     * we own the lock once the old value was "unlocked". */
    while (atomic_flag_test_and_set_explicit(&lock->locked, memory_order_acquire)) {
        arch_cpu_relax();
    }
    return irq_state;
}

static inline void spinlock_release(struct spinlock *lock, arch_irq_state_t irq_state)
{
    atomic_flag_clear_explicit(&lock->locked, memory_order_release);
    arch_irq_restore(irq_state);
}

/*
 * Variants that leave the interrupt state alone. For code that already runs
 * with interrupts disabled and must hand a lock over across a context
 * switch (the scheduler), where "restore the state I saved" has no meaning.
 */
static inline void spinlock_acquire_raw(struct spinlock *lock)
{
    while (atomic_flag_test_and_set_explicit(&lock->locked, memory_order_acquire)) {
        arch_cpu_relax();
    }
}

static inline void spinlock_release_raw(struct spinlock *lock)
{
    atomic_flag_clear_explicit(&lock->locked, memory_order_release);
}

#endif
