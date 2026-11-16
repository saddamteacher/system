#ifndef ARCH_X86_64_LAPIC_H
#define ARCH_X86_64_LAPIC_H

#include <stdint.h>

/*
 * Local APIC: the interrupt controller built into each CPU. It receives
 * interrupts from the I/O APIC, delivers them to its CPU by priority, and
 * contains a timer.
 */

/* `registers` is the mapped register page. Returns NULL or a failure text. */
const char *lapic_init(volatile uint32_t *registers);

/* This CPU's APIC id: the address the I/O APIC sends interrupts to. */
uint32_t lapic_id(void);

/* "End of interrupt": tells the APIC the current interrupt is handled, so
 * it may deliver the next one of the same or lower priority. */
void lapic_end_of_interrupt(void);

/* Measures the timer's input clock against the PIT, then starts periodic
 * interrupts on VECTOR_TIMER. Returns the measured clock in Hz. */
uint64_t lapic_timer_start(uint32_t frequency_hz);

#endif
