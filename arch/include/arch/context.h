#ifndef ARCH_CONTEXT_H
#define ARCH_CONTEXT_H

/*
 * Kernel execution contexts: what must be saved to suspend a thread in the
 * middle of a function call and resume it later.
 *
 * A suspended context is represented by its stack pointer; everything else
 * that needs saving is stored on the thread's own stack.
 */

/*
 * Prepares a fresh stack so that the first switch to it starts executing
 * `entry` (which must never return). Returns the stack pointer to pass to
 * arch_context_switch().
 */
void *arch_context_create(void *stack_top, void (*entry)(void));

/*
 * Suspends the current context, storing its stack pointer in *save, and
 * resumes the context whose stack pointer is `load`. Returns when some later
 * switch resumes the context suspended here. Call with interrupts disabled.
 */
void arch_context_switch(void **save, void *load);

#endif
