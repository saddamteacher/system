#ifndef ARCH_X86_64_ACPI_H
#define ARCH_X86_64_ACPI_H

#include <stdbool.h>
#include <stdint.h>

#include <arch/irq.h>

/*
 * The subset of ACPI the kernel needs: where the interrupt controllers are
 * and how legacy IRQ lines are wired to them. ACPI is a set of tables the
 * firmware leaves in memory describing the machine.
 */

#define ACPI_MAX_IRQ_OVERRIDES 16

/* "IRQ `source` is not wired to I/O APIC input `source` but to `gsi`." */
struct acpi_irq_override {
    uint8_t source;
    uint32_t gsi; /* Global System Interrupt: an I/O APIC input number */
    bool active_low;
    bool level_triggered;
};

struct acpi_info {
    uint64_t lapic_address;
    uint64_t ioapic_address;
    uint32_t ioapic_gsi_base; /* GSI of the I/O APIC's input 0 */
    bool has_legacy_pic;
    struct acpi_irq_override overrides[ACPI_MAX_IRQ_OVERRIDES];
    unsigned override_count;
    uint16_t pm_timer_port; /* 0 if the machine has no ACPI PM timer */
};

/* Reads the tables. Returns NULL on success or a description of the failure. */
const char *acpi_init(arch_map_device_fn map_device, struct acpi_info *info);

#endif
