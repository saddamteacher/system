/*
 * State: `ticks` has one writer, the timer interrupt handler, and any number
 * of readers. It is atomic so a reader never sees a half-updated value.
 */
#include <core/time.h>

#include <stdatomic.h>

#include <arch/irq.h>
#include <arch/timer.h>
#include <core/log.h>
#include <core/panic.h>

static atomic_uint_fast64_t ticks;

/* Runs in interrupt context, TIME_TICK_HZ times per second. */
static void on_tick(void)
{
    atomic_fetch_add_explicit(&ticks, 1, memory_order_relaxed);
}

void time_init(void)
{
    const char *problem = arch_timer_start(TIME_TICK_HZ, on_tick);
    if (problem != NULL) {
        panic("time: %s", problem);
    }
    log_info("time: %u Hz tick, timer input clock measured at %llu Hz", TIME_TICK_HZ,
             (unsigned long long)arch_timer_source_hz());
}

uint64_t time_ticks(void)
{
    return atomic_load_explicit(&ticks, memory_order_relaxed);
}

uint64_t time_now_ms(void)
{
    return time_ticks() * TIME_MS_PER_TICK;
}

void time_wait_ms(uint64_t milliseconds)
{
    uint64_t deadline = time_now_ms() + milliseconds;
    while (time_now_ms() < deadline) {
        /* Every tick wakes the CPU, so the deadline is noticed within one
         * tick even if this particular wake-up was for something else. */
        arch_cpu_idle();
    }
}
