/* Tests for interrupts, time and input devices. */
#include "tests_internal.h"

#include <stddef.h>

#include <arch/irq.h>
#include <arch/timer.h>
#include <core/log.h>
#include <core/time.h>
#include <drivers/keyboard/keyboard.h>
#include <sched/thread.h>

#define TIMER_TEST_MS 1000
#define TIMER_TOLERANCE_PERCENT 1

bool test_timer(void)
{
    struct arch_reference_clock reference;
    if (!arch_reference_clock_info(&reference)) {
        log_error("timer test: no reference clock on this machine");
        return false;
    }

    /* Start on a tick boundary so both clocks measure the same interval. */
    uint64_t start_tick = time_ticks();
    while (time_ticks() == start_tick) {
        arch_cpu_idle();
    }
    start_tick = time_ticks();
    uint32_t start_reference = arch_reference_clock_read();

    while (time_ticks() < start_tick + TIMER_TEST_MS * TIME_TICK_HZ / 1000) {
        arch_cpu_idle();
    }
    uint32_t end_reference = arch_reference_clock_read();

    /* The reference counter may have wrapped once; masking the difference
     * gives the right answer as long as it wrapped at most once. */
    uint64_t counted = (end_reference - start_reference) & reference.mask;
    uint64_t measured_us = counted * 1000000u / reference.frequency_hz;
    uint64_t expected_us = TIMER_TEST_MS * 1000u;
    uint64_t tolerance_us = expected_us * TIMER_TOLERANCE_PERCENT / 100;

    log_info("timer test: %u ticks took %llu us by the reference clock (expected %llu +/- %llu)",
             TIMER_TEST_MS * TIME_TICK_HZ / 1000, (unsigned long long)measured_us,
             (unsigned long long)expected_us, (unsigned long long)tolerance_us);
    return measured_us >= expected_us - tolerance_us && measured_us <= expected_us + tolerance_us;
}

#define KEYBOARD_TEST_TIMEOUT_MS 30000

struct expected_key {
    enum key_code code;
    char character;
};

/* What tools/qemu-sendkeys.py types for this test (see the Makefile):
 * "a", then shift+"b", then enter, then the up arrow. */
static const struct expected_key expected_presses[] = {
    { KEY_A, 'a' },
    { KEY_LEFT_SHIFT, 0 },
    { KEY_B, 'B' }, /* shift held: upper case */
    { KEY_ENTER, '\n' },
    { KEY_UP, 0 },  /* an extended (0xE0-prefixed) scancode */
};

#define EXPECTED_PRESS_COUNT (sizeof(expected_presses) / sizeof(expected_presses[0]))

bool test_keyboard(void)
{
    size_t presses = 0;
    size_t releases = 0;
    uint64_t deadline = time_now_ms() + KEYBOARD_TEST_TIMEOUT_MS;

    /* The check script waits for this line before it starts typing. */
    log_info("test: keyboard ready for input");

    while (presses < EXPECTED_PRESS_COUNT || releases < EXPECTED_PRESS_COUNT) {
        struct key_event event;
        if (!keyboard_read_event(&event)) {
            if (time_now_ms() >= deadline) {
                log_error("keyboard test: timed out after %zu presses, %zu releases", presses, releases);
                return false;
            }
            thread_sleep_ms(5);
            continue;
        }
        keyboard_log_event(&event);

        if (!event.pressed) {
            releases++;
            continue;
        }
        if (presses == EXPECTED_PRESS_COUNT || event.code != expected_presses[presses].code
            || event.character != expected_presses[presses].character) {
            log_error("keyboard test: unexpected key press number %zu", presses);
            return false;
        }
        presses++;
    }
    return true;
}
