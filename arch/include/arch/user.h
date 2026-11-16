#ifndef ARCH_USER_H
#define ARCH_USER_H

#include <stdint.h>

/*
 * The boundary between user mode and the kernel.
 */

/*
 * Leaves the kernel: starts executing user code at `entry` with the given
 * stack pointer and two arguments (argc and argv by convention). The
 * current address space must already be the program's. Never returns; the
 * thread comes back into the kernel only through interrupts and system
 * calls, on its kernel stack.
 */
_Noreturn void arch_enter_user(uintptr_t entry, uintptr_t stack_pointer, uint64_t argument0, uint64_t argument1);

/*
 * Upcall, implemented by the kernel (kernel/syscall). Called for every
 * system call with interrupts enabled, on the calling thread's kernel
 * stack. The return value is handed back to the program.
 */
#define ARCH_SYSCALL_ARGUMENT_COUNT 6
int64_t kernel_syscall(uint64_t number, const uint64_t arguments[ARCH_SYSCALL_ARGUMENT_COUNT]);

#endif
