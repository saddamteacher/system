#ifndef ARCH_CPU_H
#define ARCH_CPU_H

#include <stdint.h>

/*
 * Installs the CPU's own tables (on x86_64: GDT, TSS, IDT) so that traps are
 * delivered to the kernel. Call once, early, on the boot CPU.
 */
void arch_cpu_init(void);

/* Stops this CPU permanently, with interrupts disabled. */
_Noreturn void arch_halt_forever(void);

/* Hint for busy-wait loops: lets the CPU save power / yield to a sibling. */
void arch_cpu_relax(void);

/*
 * Interrupt masking. arch_irq_save() disables interrupts and returns the
 * previous state; arch_irq_restore() puts that state back. Using save/restore
 * instead of disable/enable makes critical sections nest correctly.
 */
typedef uint64_t arch_irq_state_t;
arch_irq_state_t arch_irq_save(void);
void arch_irq_restore(arch_irq_state_t state);

/*
 * Abandons the current stack: continues in `entry` with the stack pointer at
 * `stack_top`. Used once at boot to leave the bootloader's stack.
 */
_Noreturn void arch_call_on_stack(void *stack_top, void (*entry)(void));

/* Raises a breakpoint trap at the call site. Execution continues after it. */
void arch_breakpoint(void);

/*
 * Asks the emulator to terminate with the given code. Used by automated
 * checks so a test run ends by itself instead of waiting for a timeout.
 * Has no effect when no debug-exit device is present (e.g. `make run`),
 * so callers must still halt afterwards.
 */
#define ARCH_DEBUG_EXIT_SUCCESS 0x10u
#define ARCH_DEBUG_EXIT_FAILURE 0x11u
void arch_debug_exit(uint32_t code);

#endif
