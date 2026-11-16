#include <arch/cpu.h>

#include "io.h"

/* I/O port of QEMU's isa-debug-exit device, as configured in the Makefile. */
#define DEBUG_EXIT_PORT 0xF4

_Noreturn void arch_halt_forever(void)
{
    __asm__ volatile("cli");
    for (;;) {
        /* hlt sleeps until the next interrupt. A non-maskable interrupt can
         * still wake the CPU even with interrupts disabled, hence the loop. */
        __asm__ volatile("hlt");
    }
}

void arch_debug_exit(uint32_t code)
{
    /* QEMU exits with status (code << 1) | 1. Without the device the port is
     * unassigned and the write is ignored. */
    io_out32(DEBUG_EXIT_PORT, code);
}
