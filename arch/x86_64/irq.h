#ifndef ARCH_X86_64_IRQ_H
#define ARCH_X86_64_IRQ_H

#include <stdbool.h>
#include <stdint.h>

/* Handles an interrupt vector outside the CPU exception range. Returns
 * false if the vector is not one the kernel set up. */
bool irq_dispatch(uint64_t vector);

#endif
