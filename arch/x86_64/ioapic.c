/*
 * The I/O APIC has only two directly visible registers: an index register
 * and a data window through which the indexed register is read or written.
 *
 * State: set once in ioapic_init(); the redirection table is written by
 * ioapic_route(), whose caller (arch_irq_register) serialises it.
 */
#include "ioapic.h"

#define REG_INDEX  0x00
#define REG_WINDOW 0x10

#define INDEX_VERSION 0x01
/* Each input has a 64-bit redirection entry, split over two registers. */
#define INDEX_REDIRECTION(input) (0x10 + 2 * (input))

#define REDIRECTION_ACTIVE_LOW      (1u << 13)
#define REDIRECTION_LEVEL_TRIGGERED (1u << 15)
#define REDIRECTION_MASKED          (1u << 16)

static volatile uint32_t *registers;
static uint32_t gsi_base;
static uint32_t input_count;

static uint32_t reg_read(uint32_t index)
{
    registers[REG_INDEX / sizeof(uint32_t)] = index;
    return registers[REG_WINDOW / sizeof(uint32_t)];
}

static void reg_write(uint32_t index, uint32_t value)
{
    registers[REG_INDEX / sizeof(uint32_t)] = index;
    registers[REG_WINDOW / sizeof(uint32_t)] = value;
}

void ioapic_init(volatile uint32_t *mapped_registers, uint32_t first_gsi)
{
    registers = mapped_registers;
    gsi_base = first_gsi;
    /* Bits 16-23 of the version register hold the highest input number. */
    input_count = ((reg_read(INDEX_VERSION) >> 16) & 0xFF) + 1;

    for (uint32_t input = 0; input < input_count; input++) {
        reg_write(INDEX_REDIRECTION(input), REDIRECTION_MASKED);
        reg_write(INDEX_REDIRECTION(input) + 1, 0);
    }
}

bool ioapic_route(uint32_t gsi, uint8_t vector, uint32_t lapic_id, bool active_low, bool level_triggered)
{
    if (gsi < gsi_base || gsi - gsi_base >= input_count) {
        return false;
    }
    uint32_t input = gsi - gsi_base;

    uint32_t low = vector;
    if (active_low) {
        low |= REDIRECTION_ACTIVE_LOW;
    }
    if (level_triggered) {
        low |= REDIRECTION_LEVEL_TRIGGERED;
    }
    /* Write the destination first: the low half carries the mask bit, so
     * the entry becomes live only when it is complete. */
    reg_write(INDEX_REDIRECTION(input) + 1, lapic_id << 24);
    reg_write(INDEX_REDIRECTION(input), low);
    return true;
}
