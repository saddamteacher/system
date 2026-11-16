#ifndef ARCH_X86_64_IOAPIC_H
#define ARCH_X86_64_IOAPIC_H

#include <stdbool.h>
#include <stdint.h>

/*
 * I/O APIC: collects interrupt lines from devices and forwards each, as a
 * chosen vector, to a chosen CPU's local APIC.
 */

/* `registers` is the mapped register page; `gsi_base` is the global number
 * of input 0. All inputs are masked. */
void ioapic_init(volatile uint32_t *registers, uint32_t gsi_base);

/* Routes input `gsi` to `vector` on the CPU with local APIC `lapic_id` and
 * unmasks it. Returns false if this I/O APIC has no such input. */
bool ioapic_route(uint32_t gsi, uint8_t vector, uint32_t lapic_id, bool active_low, bool level_triggered);

#endif
