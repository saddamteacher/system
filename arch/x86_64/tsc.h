#ifndef ARCH_X86_64_TSC_H
#define ARCH_X86_64_TSC_H

/*
 * Time Stamp Counter: a 64-bit counter in the CPU that advances at a fixed
 * rate and can be read in a few nanoseconds with one instruction. It is the
 * kernel's high-resolution clock.
 */

/* Measures the counter's rate against the PIT. Interrupts must be off. */
void tsc_calibrate(void);

#endif
