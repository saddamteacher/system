/* Time as seen from user space. */
#include <stdio.h>
#include <unistd.h>

int main(void)
{
    unsigned long long before = uptime_ms();
    if (sleep_ms(80) != 0) {
        return 1;
    }
    unsigned long long elapsed = uptime_ms() - before;
    printf("sleep: asked for 80 ms, slept %llu ms\n", elapsed);
    return elapsed >= 80 && elapsed <= 100 ? 0 : 1;
}
