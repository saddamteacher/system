#ifndef ARCH_X86_64_VECTORS_H
#define ARCH_X86_64_VECTORS_H

/*
 * How the 256 interrupt vectors are used.
 *
 *   0x00-0x1F  CPU exceptions (fixed by the architecture)
 *   0x20-0x2F  legacy PIC, remapped here and masked; only spurious
 *              interrupts can still arrive
 *   0x30       local APIC timer
 *   0x31-0x40  device IRQs 0-15, routed through the I/O APIC
 *   0xFF       local APIC spurious interrupt
 */
#define VECTOR_PIC_BASE      0x20
#define VECTOR_PIC_COUNT     16
#define VECTOR_TIMER         0x30
#define VECTOR_IRQ_BASE      0x31
#define VECTOR_SPURIOUS      0xFF

#endif
