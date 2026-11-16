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
/* Written once, during boot, before the first thread is created. */
static void (*tick_hook)(void);

/* Runs in interrupt context, TIME_TICK_HZ times per second. */
static void on_tick(void)
{
    atomic_fetch_add_explicit(&ticks, 1, memory_order_relaxed);
    if (tick_hook != NULL) {
        tick_hook();
    }
}

void time_set_tick_hook(void (*hook)(void))
{
    tick_hook = hook;
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

uint64_t time_now_ns(void)
{
    return arch_clock_ns();
}
