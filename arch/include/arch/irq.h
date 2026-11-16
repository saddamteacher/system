#ifndef ARCH_IRQ_H
#define ARCH_IRQ_H

#include <stdbool.h>
#include <stddef.h>

#include <arch/mmu.h>

/*
 * Hardware interrupts from devices.
 *
 * An IRQ number here is the platform's conventional line number (on a PC the
 * legacy ISA numbering: 1 = keyboard, 12 = mouse). How a line reaches the CPU
 * is the arch layer's business.
 */

/*
 * Maps device registers (physical memory that is not RAM) and returns a
 * kernel pointer to them, or NULL. Provided by the kernel's memory manager.
 */
typedef void *(*arch_map_device_fn)(phys_addr_t address, size_t size);

/*
 * Finds and initialises the interrupt controllers. All device interrupts
 * start disabled. Returns NULL on success or a description of the failure.
 * Call once, on the boot CPU, with interrupts disabled.
 */
const char *arch_irq_init(arch_map_device_fn map_device);

/*
 * Handlers run in interrupt context: interrupts disabled, on the stack of
 * whatever was interrupted. They must be short and must not block.
 */
typedef void (*arch_irq_handler_fn)(void *context);

#define ARCH_IRQ_COUNT 16

/*
 * Installs the handler for an IRQ line and enables the line. One handler per
 * line. Returns false if the line is invalid, taken, or cannot be routed.
 */
bool arch_irq_register(unsigned irq, arch_irq_handler_fn handler, void *context);

/* Lets the CPU accept interrupts. */
void arch_irq_enable(void);

/*
 * Enables interrupts and sleeps until the next one arrives. The two steps
 * are atomic: an interrupt cannot slip in between and be missed.
 */
void arch_cpu_idle(void);

#endif
