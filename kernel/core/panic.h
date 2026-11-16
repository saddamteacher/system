#ifndef CORE_PANIC_H
#define CORE_PANIC_H

/*
 * Stops the kernel after an unrecoverable error: prints the message and a
 * stack trace, then halts. Use it for conditions that mean a kernel bug or a
 * machine we cannot run on, never for errors a caller could handle.
 */
_Noreturn void panic(const char *format, ...) __attribute__((format(printf, 1, 2)));

#endif
