#ifndef CORE_PANIC_H
#define CORE_PANIC_H

/*
 * Stops the kernel after an unrecoverable error: prints the message and a
 * stack trace, then halts. Use it for conditions that mean a kernel bug or a
 * machine we cannot run on, never for errors a caller could handle.
 */
_Noreturn void panic(const char *format, ...) __attribute__((format(printf, 1, 2)));

/*
 * A CPU exception raised by user mode code is the program's problem, not
 * the kernel's. The process layer installs a handler that ends the
 * offending process; it must not return. Until one is installed (there is
 * no user code yet) such a trap panics like any other.
 */
struct arch_trap_frame;
struct arch_trap_info;
typedef void (*user_fault_handler_fn)(const struct arch_trap_frame *frame, const struct arch_trap_info *info);
void panic_set_user_fault_handler(user_fault_handler_fn handler);

#endif
