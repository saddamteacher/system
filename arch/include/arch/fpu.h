#ifndef ARCH_FPU_H
#define ARCH_FPU_H

#include <stddef.h>

/*
 * Floating point and vector register state.
 *
 * The kernel itself never uses these registers, but user programs do. They
 * are not part of the kernel context switch, so the scheduler saves and
 * restores them for threads that run user code, into a buffer per thread.
 */

/* Size and required alignment of a state buffer. */
size_t arch_fpu_state_size(void);
#define ARCH_FPU_STATE_ALIGNMENT 16

/* Fills a buffer with the state a new program starts with. */
void arch_fpu_state_init(void *state);

void arch_fpu_save(void *state);
void arch_fpu_restore(const void *state);

#endif
