#ifndef CORE_TIME_H
#define CORE_TIME_H

#include <stdint.h>

/*
 * Kernel time base: a counter incremented by the periodic timer interrupt.
 * It starts at 0 when time_init() runs and never goes backwards.
 */

#define TIME_TICK_HZ 1000u
#define TIME_MS_PER_TICK (1000u / TIME_TICK_HZ)

/* Starts the tick. Requires arch_irq_init(). Call once. */
void time_init(void);

uint64_t time_ticks(void);
uint64_t time_now_ms(void);

/*
 * Sleeps the CPU until `milliseconds` have passed. Interrupts are enabled
 * while waiting and stay enabled afterwards. A stop-gap until threads exist
 * (Phase 4): it blocks the whole kernel.
 */
void time_wait_ms(uint64_t milliseconds);

#endif
