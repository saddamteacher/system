/* Time as seen from user space. */
#include <stdio.h>
#include <unistd.h>

int main(void)
{
    unsigned long long before = uptime_ms();
    unsigned long long before_ns = clock_ns();
    if (sleep_ms(80) != 0) {
        return 1;
    }
    unsigned long long elapsed = uptime_ms() - before;
    unsigned long long elapsed_us = (clock_ns() - before_ns) / 1000;
    printf("sleep: asked for 80 ms, slept %llu ms by the tick, %llu us by the fine clock\n", elapsed, elapsed_us);

    /* The two clocks are independent (timer interrupt and CPU counter);
     * they must agree to within a couple of ticks. */
    if (elapsed_us < 79000 || elapsed_us > 102000) {
        return 1;
    }
    /* The fine clock never goes backwards. */
    unsigned long long previous = clock_ns();
    for (int i = 0; i < 1000; i++) {
        unsigned long long now = clock_ns();
        if (now < previous) {
            return 1;
        }
        previous = now;
    }
    /* Calendar time is plausible: after this code was written, and not
     * absurdly far in the future. */
    unsigned long long calendar = wall_time();
    printf("sleep: calendar time is %llu seconds since 1970\n", calendar);
    if (calendar < 1700000000ull || calendar > 4102444800ull) {
        return 1;
    }
    return elapsed >= 80 && elapsed <= 100 ? 0 : 1;
}
