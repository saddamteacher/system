/*
 * State: `registers` is set once in lapic_init(). The registers themselves
 * are per-CPU hardware state; only the boot CPU exists so far.
 */
#include "lapic.h"

#include <stddef.h>

#include "msr.h"
#include "pit.h"
#include "vectors.h"

/* Register offsets in bytes; each register is 32 bits wide. */
#define REG_ID                0x020
#define REG_TASK_PRIORITY     0x080
#define REG_EOI               0x0B0
#define REG_SPURIOUS          0x0F0
#define REG_LVT_TIMER         0x320
#define REG_TIMER_INITIAL     0x380
#define REG_TIMER_CURRENT     0x390
#define REG_TIMER_DIVIDE      0x3E0

#define MSR_APIC_BASE          0x1Bu
#define APIC_BASE_X2APIC       (1ULL << 10)
#define APIC_BASE_ENABLE       (1ULL << 11)

#define SPURIOUS_APIC_ENABLE   (1u << 8)
#define LVT_MASKED             (1u << 16)
#define LVT_TIMER_PERIODIC     (1u << 17)
/* The timer counts its input clock divided by this setting. */
#define TIMER_DIVIDE_BY_16     0x3

/*
 * A calibration run takes two samples, this many PIT ticks after the PIT
 * was started: about 2 ms and 46 ms, inside the PIT's 54.9 ms range.
 */
#define CALIBRATION_FIRST_SAMPLE  2386u
#define CALIBRATION_SECOND_SAMPLE 54886u
#define CALIBRATION_RUNS 3

static volatile uint32_t *registers;

static uint32_t reg_read(unsigned offset)
{
    return registers[offset / sizeof(uint32_t)];
}

static void reg_write(unsigned offset, uint32_t value)
{
    registers[offset / sizeof(uint32_t)] = value;
}

const char *lapic_init(volatile uint32_t *mapped_registers)
{
    uint64_t base = msr_read(MSR_APIC_BASE);
    if (base & APIC_BASE_X2APIC) {
        /* In x2APIC mode the registers are MSRs and the memory-mapped page
         * we rely on is switched off. */
        return "local APIC is in x2APIC mode, which is not supported";
    }
    msr_write(MSR_APIC_BASE, base | APIC_BASE_ENABLE);
    registers = mapped_registers;

    /* Accept interrupts of every priority. */
    reg_write(REG_TASK_PRIORITY, 0);
    reg_write(REG_LVT_TIMER, LVT_MASKED);
    /* The APIC is enabled by software in the same register that sets the
     * vector for spurious interrupts. */
    reg_write(REG_SPURIOUS, SPURIOUS_APIC_ENABLE | VECTOR_SPURIOUS);
    return NULL;
}

uint32_t lapic_id(void)
{
    return reg_read(REG_ID) >> 24;
}

void lapic_end_of_interrupt(void)
{
    reg_write(REG_EOI, 0);
}

struct calibration_sample {
    uint16_t pit;   /* PIT countdown value */
    uint32_t timer; /* local APIC timer count, read right after it */
};

/* Waits until the PIT has counted `pit_ticks` down from its start value,
 * then reads both counters. */
static struct calibration_sample sample_after(uint16_t pit_ticks)
{
    struct calibration_sample sample;
    do {
        __asm__ volatile("pause");
        sample.pit = pit_read_countdown();
    } while ((uint16_t)(PIT_COUNTDOWN_START - sample.pit) < pit_ticks);
    sample.timer = reg_read(REG_TIMER_CURRENT);
    return sample;
}

/*
 * Measures how fast the timer counts, in counts per second, by letting it
 * run next to the PIT, whose speed is known. The rate is the ratio of how
 * far each counter moved between two samples.
 *
 * Both samples are taken by the same code (sample_after). The two counters
 * cannot be read at the same instant, but as long as the gap between the
 * reads is the same in both samples it cancels out of the differences.
 * Taking the first sample "by hand" before the loop instead left a fixed
 * skew of about 50 microseconds, a 0.1-0.9% error depending on run length.
 */
static uint64_t measure_timer_rate(void)
{
    pit_start_countdown();
    reg_write(REG_TIMER_INITIAL, 0xFFFFFFFFu);

    struct calibration_sample first = sample_after(CALIBRATION_FIRST_SAMPLE);
    struct calibration_sample second = sample_after(CALIBRATION_SECOND_SAMPLE);

    reg_write(REG_TIMER_INITIAL, 0); /* stop */

    /* Both count down, so "first - second" is the elapsed amount. */
    uint64_t timer_elapsed = first.timer - second.timer;
    uint64_t pit_elapsed = (uint16_t)(first.pit - second.pit);
    return timer_elapsed * PIT_FREQUENCY_HZ / pit_elapsed;
}

static uint64_t median_of_three(uint64_t a, uint64_t b, uint64_t c)
{
    if ((a >= b) == (a <= c)) {
        return a;
    }
    return ((b >= a) == (b <= c)) ? b : c;
}

uint64_t lapic_timer_start(uint32_t frequency_hz)
{
    reg_write(REG_TIMER_DIVIDE, TIMER_DIVIDE_BY_16);
    reg_write(REG_LVT_TIMER, LVT_MASKED);

    /* A single run can be disturbed (in an emulator, by the host being
     * busy between two reads). The median of three discards one outlier in
     * either direction. */
    uint64_t rates[CALIBRATION_RUNS];
    for (int run = 0; run < CALIBRATION_RUNS; run++) {
        rates[run] = measure_timer_rate();
    }
    uint64_t counts_per_second = median_of_three(rates[0], rates[1], rates[2]);

    reg_write(REG_LVT_TIMER, LVT_TIMER_PERIODIC | VECTOR_TIMER);
    reg_write(REG_TIMER_INITIAL, (uint32_t)(counts_per_second / frequency_hz));
    return counts_per_second * 16;
}
