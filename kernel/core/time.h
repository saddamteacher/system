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
 * Installs a function called on every tick, in interrupt context, after the
 * counter was advanced. One hook; the scheduler uses it for preemption.
 * Passing a layer above core in by pointer keeps core independent of it.
 */
void time_set_tick_hook(void (*hook)(void));

#endif
