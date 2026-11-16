#include <arch/cpu.h>

#include "gdt.h"
#include "idt.h"
#include "fpu.h"
#include "io.h"
#include "syscall.h"

/* I/O port of QEMU's isa-debug-exit device, as configured in the Makefile. */
#define DEBUG_EXIT_PORT 0xF4

/* Interrupt-enable flag (IF) in RFLAGS. */
#define RFLAGS_IF (1ULL << 9)

void arch_cpu_init(void)
{
    /* The IDT's gates name the kernel code segment, so the GDT goes first. */
    gdt_init();
    idt_init();
    fpu_init();
    syscall_init();
}

_Noreturn void arch_halt_forever(void)
{
    __asm__ volatile("cli");
    for (;;) {
        /* hlt sleeps until the next interrupt. A non-maskable interrupt can
         * still wake the CPU even with interrupts disabled, hence the loop. */
        __asm__ volatile("hlt");
    }
}

void arch_cpu_relax(void)
{
    __asm__ volatile("pause");
}

arch_irq_state_t arch_irq_save(void)
{
    uint64_t rflags;
    /* There is no instruction that reads RFLAGS directly; go via the stack. */
    __asm__ volatile("pushfq; popq %0; cli" : "=r"(rflags));
    return rflags & RFLAGS_IF;
}

void arch_irq_restore(arch_irq_state_t state)
{
    if (state & RFLAGS_IF) {
        __asm__ volatile("sti");
    }
}

void arch_breakpoint(void)
{
    __asm__ volatile("int3");
}

void arch_debug_exit(uint32_t code)
{
    /* QEMU exits with status (code << 1) | 1. Without the device the port is
     * unassigned and the write is ignored. */
    io_out32(DEBUG_EXIT_PORT, code);
}
