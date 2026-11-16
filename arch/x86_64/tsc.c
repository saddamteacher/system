/*
 * State: tsc_base and ns_per_tick_fixed are written once by
 * tsc_calibrate() during single-threaded boot.
 */
#include "tsc.h"

#include <stdint.h>

#include <arch/timer.h>

#include "pit.h"

/* Same two-sample method as the local APIC timer calibration (lapic.c):
 * both samples are taken by the same code so constant delays cancel. */
#define CALIBRATION_FIRST_SAMPLE  2386u
#define CALIBRATION_SECOND_SAMPLE 54886u

/* Nanoseconds per counter tick as a 32.32 fixed-point number. Multiplying
 * by it and shifting avoids a 128-bit division on every clock read. */
#define FIXED_SHIFT 32

static uint64_t tsc_base;
static uint64_t ns_per_tick_fixed;

static uint64_t read_tsc(void)
{
    uint32_t low, high;
    __asm__ volatile("rdtsc" : "=a"(low), "=d"(high));
    return ((uint64_t)high << 32) | low;
}

struct sample {
    uint16_t pit;
    uint64_t tsc;
};

static struct sample sample_after(uint16_t pit_ticks)
{
    struct sample sample;
    do {
        __asm__ volatile("pause");
        sample.pit = pit_read_countdown();
    } while ((uint16_t)(PIT_COUNTDOWN_START - sample.pit) < pit_ticks);
    sample.tsc = read_tsc();
    return sample;
}

void tsc_calibrate(void)
{
    pit_start_countdown();
    struct sample first = sample_after(CALIBRATION_FIRST_SAMPLE);
    struct sample second = sample_after(CALIBRATION_SECOND_SAMPLE);

    uint64_t tsc_elapsed = second.tsc - first.tsc;
    uint64_t pit_elapsed = (uint16_t)(first.pit - second.pit);
    /* ns per TSC tick = (pit_elapsed / PIT_HZ * 1e9) / tsc_elapsed. */
    uint64_t elapsed_ns = pit_elapsed * 1000000000ull / PIT_FREQUENCY_HZ;
    ns_per_tick_fixed = tsc_elapsed != 0 ? (elapsed_ns << FIXED_SHIFT) / tsc_elapsed : 0;
    tsc_base = read_tsc();
}

uint64_t arch_clock_ns(void)
{
    uint64_t ticks = read_tsc() - tsc_base;
    /* A 64x64 -> 128 bit multiplication is a single instruction. */
    return (uint64_t)(((unsigned __int128)ticks * ns_per_tick_fixed) >> FIXED_SHIFT);
}
