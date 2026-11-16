#ifndef ARCH_CPU_H
#define ARCH_CPU_H

#include <stdint.h>

/* Stops this CPU permanently, with interrupts disabled. */
_Noreturn void arch_halt_forever(void);

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
