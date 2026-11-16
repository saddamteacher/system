#ifndef ARCH_TIMER_H
#define ARCH_TIMER_H

#include <stdbool.h>
#include <stdint.h>

/*
 * Starts a periodic timer interrupt at `frequency_hz` that calls `on_tick`
 * in interrupt context. Requires arch_irq_init(). Returns NULL on success or
 * a description of the failure.
 */
const char *arch_timer_start(uint32_t frequency_hz, void (*on_tick)(void));

/* Input clock of the tick timer as measured at start-up, for diagnostics. */
uint64_t arch_timer_source_hz(void);

/*
 * High-resolution monotonic clock: nanoseconds since arch_timer_start().
 * Cheap to read and far finer than the tick; use it to measure durations.
 */
uint64_t arch_clock_ns(void);

/*
 * The current date and time from the machine's battery-backed clock, as
 * seconds since 1970-01-01 00:00:00 UTC. Slow; read it once at boot.
 */
uint64_t arch_wall_clock_seconds(void);

/*
 * An independent free-running hardware counter, not derived from the tick
 * timer. Its only use is to verify that ticks arrive at the right rate.
 * It counts up at `frequency_hz` and wraps at `mask`.
 */
struct arch_reference_clock {
    uint32_t frequency_hz;
    uint32_t mask;
};

bool arch_reference_clock_info(struct arch_reference_clock *info);
uint32_t arch_reference_clock_read(void);

#endif
