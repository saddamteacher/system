/*
 * Interrupt Descriptor Table: for each of the 256 interrupt vectors, the
 * address the CPU jumps to and how it gets there.
 *
 * State: `idt` is written once in idt_init() during single-threaded boot and
 * is read-only afterwards.
 */
#include "idt.h"

#include <stdint.h>

#include "gdt.h"
#include "trap.h"

/* Present, ring 0, type 0xE = 64-bit interrupt gate. Unlike a trap gate, an
 * interrupt gate clears the interrupt flag on entry, so a handler is never
 * interrupted before it has saved the registers. */
#define GATE_INTERRUPT 0x8E

/* Layout fixed by the CPU. The handler address is split into three parts. */
struct idt_gate {
    uint16_t offset_low;
    uint16_t selector;
    uint8_t ist; /* 0 = stay on the current stack, 1-7 = IST slot */
    uint8_t type_attributes;
    uint16_t offset_middle;
    uint32_t offset_high;
    uint32_t reserved;
} __attribute__((packed));

struct descriptor_table_pointer {
    uint16_t limit;
    uint64_t base;
} __attribute__((packed));

static struct idt_gate idt[IDT_VECTOR_COUNT];

/* Entry stubs, one per vector, generated in isr.asm. */
extern const uint64_t isr_stub_table[IDT_VECTOR_COUNT];

static void idt_set_gate(unsigned vector, uint64_t handler, uint8_t ist)
{
    idt[vector] = (struct idt_gate){
        .offset_low = handler & 0xFFFF,
        .selector = GDT_KERNEL_CODE,
        .ist = ist,
        .type_attributes = GATE_INTERRUPT,
        .offset_middle = (handler >> 16) & 0xFFFF,
        .offset_high = (uint32_t)(handler >> 32),
    };
}

static uint8_t ist_for_vector(unsigned vector)
{
    switch (vector) {
    /* A double fault often means the kernel stack itself is broken (e.g.
     * overflowed). Handling it on that same stack would fault again and
     * reset the machine, so it gets a dedicated stack. */
    case VECTOR_DOUBLE_FAULT:
        return IST_DOUBLE_FAULT;
    /* An NMI can arrive at any instruction, including ones where the stack
     * pointer is momentarily not a valid kernel stack. */
    case VECTOR_NMI:
        return IST_NMI;
    default:
        return 0;
    }
}

void idt_init(void)
{
    for (unsigned vector = 0; vector < IDT_VECTOR_COUNT; vector++) {
        idt_set_gate(vector, isr_stub_table[vector], ist_for_vector(vector));
    }

    struct descriptor_table_pointer pointer = {
        .limit = sizeof(idt) - 1,
        .base = (uint64_t)(uintptr_t)idt,
    };
    __asm__ volatile("lidt %0" : : "m"(pointer));
}
