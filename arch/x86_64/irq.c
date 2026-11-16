/*
 * Device interrupt setup and dispatch.
 *
 * State:
 *   acpi, timer_tick, timer_source_hz
 *             written during single-threaded boot, read-only afterwards.
 *   handlers  written by arch_irq_register() with interrupts disabled; read
 *             by irq_dispatch() in interrupt context. On one CPU that is
 *             sufficient: the reader cannot run while the writer does.
 */
#include <arch/irq.h>
#include <arch/timer.h>

#include <arch/cpu.h>

#include "acpi.h"
#include "io.h"
#include "ioapic.h"
#include "irq.h"
#include "lapic.h"
#include "pic.h"
#include "tsc.h"
#include "vectors.h"

/* The ACPI PM timer runs at exactly three times the PIT's input clock. */
#define PM_TIMER_FREQUENCY_HZ 3579545u
/* It is at least 24 bits wide on every machine. */
#define PM_TIMER_MASK 0x00FFFFFFu

struct irq_slot {
    arch_irq_handler_fn handler;
    void *context;
};

static struct acpi_info acpi;
static struct irq_slot handlers[ARCH_IRQ_COUNT];
static void (*timer_tick)(void);
static uint64_t timer_source_hz;

const char *arch_irq_init(arch_map_device_fn map_device)
{
    const char *problem = acpi_init(map_device, &acpi);
    if (problem != NULL) {
        return problem;
    }

    volatile uint32_t *lapic_registers = map_device(acpi.lapic_address, ARCH_PAGE_SIZE);
    volatile uint32_t *ioapic_registers = map_device(acpi.ioapic_address, ARCH_PAGE_SIZE);
    if (lapic_registers == NULL || ioapic_registers == NULL) {
        return "could not map the APIC registers";
    }

    /* Even a machine that claims to have no legacy PIC is not harmed by
     * this, and one that has it must not be left with it unmasked. */
    pic_disable();

    problem = lapic_init(lapic_registers);
    if (problem != NULL) {
        return problem;
    }
    ioapic_init(ioapic_registers, acpi.ioapic_gsi_base);
    return NULL;
}

/* Legacy IRQ n is wired to I/O APIC input n, edge triggered and active
 * high, unless the firmware lists an exception. */
static struct acpi_irq_override wiring_for_irq(unsigned irq)
{
    for (unsigned i = 0; i < acpi.override_count; i++) {
        if (acpi.overrides[i].source == irq) {
            return acpi.overrides[i];
        }
    }
    return (struct acpi_irq_override){ .source = (uint8_t)irq, .gsi = irq };
}

bool arch_irq_register(unsigned irq, arch_irq_handler_fn handler, void *context)
{
    if (irq >= ARCH_IRQ_COUNT || handler == NULL) {
        return false;
    }
    arch_irq_state_t irq_state = arch_irq_save();

    bool registered = false;
    if (handlers[irq].handler == NULL) {
        /* Install the handler before unmasking the line: the interrupt may
         * fire the moment it is routed. */
        handlers[irq] = (struct irq_slot){ .handler = handler, .context = context };

        struct acpi_irq_override wiring = wiring_for_irq(irq);
        registered = ioapic_route(wiring.gsi, (uint8_t)(VECTOR_IRQ_BASE + irq), lapic_id(), wiring.active_low,
                                  wiring.level_triggered);
        if (!registered) {
            handlers[irq] = (struct irq_slot){ 0 };
        }
    }

    arch_irq_restore(irq_state);
    return registered;
}

void arch_irq_enable(void)
{
    __asm__ volatile("sti");
}

void arch_cpu_idle(void)
{
    /* `sti` takes effect only after the following instruction, so no
     * interrupt can arrive between enabling and halting. If one could, a
     * wake-up that came in that gap would be lost and we would sleep on. */
    __asm__ volatile("sti; hlt");
}

bool irq_dispatch(uint64_t vector)
{
    if (vector == VECTOR_SPURIOUS) {
        /* The APIC raised an interrupt and withdrew it. Nothing to do, and
         * by specification no end-of-interrupt either. */
        return true;
    }
    if (vector >= VECTOR_PIC_BASE && vector < VECTOR_PIC_BASE + VECTOR_PIC_COUNT) {
        /* A spurious interrupt from the masked legacy PIC. */
        return true;
    }

    bool is_timer = vector == VECTOR_TIMER;
    bool is_device = vector >= VECTOR_IRQ_BASE && vector < VECTOR_IRQ_BASE + ARCH_IRQ_COUNT;
    if (!is_timer && !is_device) {
        return false;
    }

    /* Acknowledge before running the handler. A handler may not return here
     * for a long time (the timer handler will switch threads in Phase 4),
     * and until the acknowledgement the APIC holds back further interrupts.
     * This CPU cannot be interrupted again anyway: interrupts are off. */
    lapic_end_of_interrupt();

    if (is_timer) {
        if (timer_tick != NULL) {
            timer_tick();
        }
        return true;
    }
    const struct irq_slot *slot = &handlers[vector - VECTOR_IRQ_BASE];
    if (slot->handler != NULL) {
        slot->handler(slot->context);
    }
    return true;
}

const char *arch_timer_start(uint32_t frequency_hz, void (*on_tick)(void))
{
    if (frequency_hz == 0 || on_tick == NULL) {
        return "invalid timer configuration";
    }
    timer_tick = on_tick;
    tsc_calibrate();
    timer_source_hz = lapic_timer_start(frequency_hz);
    if (timer_source_hz == 0) {
        return "local APIC timer does not count";
    }
    return NULL;
}

uint64_t arch_timer_source_hz(void)
{
    return timer_source_hz;
}

bool arch_reference_clock_info(struct arch_reference_clock *info)
{
    if (acpi.pm_timer_port == 0) {
        return false;
    }
    *info = (struct arch_reference_clock){
        .frequency_hz = PM_TIMER_FREQUENCY_HZ,
        .mask = PM_TIMER_MASK,
    };
    return true;
}

uint32_t arch_reference_clock_read(void)
{
    return io_in32(acpi.pm_timer_port) & PM_TIMER_MASK;
}
