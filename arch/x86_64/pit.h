#ifndef ARCH_X86_64_PIT_H
#define ARCH_X86_64_PIT_H

#include <stdint.h>

/*
 * Programmable Interval Timer (8254). Its input clock is the same on every
 * PC, which makes it the yardstick for measuring timers whose speed is not
 * known in advance.
 */
#define PIT_FREQUENCY_HZ 1193182u

/* The counter is 16 bits wide, so one countdown lasts at most ~54.9 ms. */
#define PIT_COUNTDOWN_START 0xFFFFu

/* Starts counting down from PIT_COUNTDOWN_START at PIT_FREQUENCY_HZ. */
void pit_start_countdown(void);

/* Current value of the countdown. */
uint16_t pit_read_countdown(void);

#endif
