/*
 * Real-time clock (the battery-backed CMOS clock of the PC), read once at
 * boot to learn the current date and time.
 *
 * It is reached through two ports: write a register number to one, read
 * the value from the other.
 */
#include <arch/timer.h>

#include <stdbool.h>

#include "io.h"

#define CMOS_INDEX 0x70
#define CMOS_DATA  0x71

#define REG_SECONDS  0x00
#define REG_MINUTES  0x02
#define REG_HOURS    0x04
#define REG_DAY      0x07
#define REG_MONTH    0x08
#define REG_YEAR     0x09
#define REG_STATUS_A 0x0A
#define REG_STATUS_B 0x0B

#define STATUS_A_UPDATING (1u << 7) /* the clock is changing its registers */
#define STATUS_B_24_HOUR  (1u << 1)
#define STATUS_B_BINARY   (1u << 2) /* values are binary, not BCD */
#define HOURS_PM_FLAG     0x80      /* in 12-hour mode */

struct rtc_time {
    uint8_t seconds, minutes, hours, day, month, year;
};

static uint8_t cmos_read(uint8_t reg)
{
    io_out8(CMOS_INDEX, reg);
    return io_in8(CMOS_DATA);
}

static void read_registers(struct rtc_time *time)
{
    /* Reading while the clock ticks over could mix old and new values. */
    while (cmos_read(REG_STATUS_A) & STATUS_A_UPDATING) {
    }
    time->seconds = cmos_read(REG_SECONDS);
    time->minutes = cmos_read(REG_MINUTES);
    time->hours = cmos_read(REG_HOURS);
    time->day = cmos_read(REG_DAY);
    time->month = cmos_read(REG_MONTH);
    time->year = cmos_read(REG_YEAR);
}

static bool same_time(const struct rtc_time *a, const struct rtc_time *b)
{
    return a->seconds == b->seconds && a->minutes == b->minutes && a->hours == b->hours && a->day == b->day
        && a->month == b->month && a->year == b->year;
}

/* Binary-coded decimal: 0x59 means 59. */
static unsigned from_bcd(uint8_t value)
{
    return (value & 0x0F) + (value >> 4) * 10u;
}

/* Days from 1970-01-01 to the given date in the Gregorian calendar. */
static uint64_t days_since_epoch(unsigned year, unsigned month, unsigned day)
{
    /* Count years from March, so the leap day is the last day of a year
     * and month lengths follow a simple pattern. */
    if (month <= 2) {
        year--;
        month += 12;
    }
    uint64_t era_days = 365ull * year + year / 4 - year / 100 + year / 400;
    uint64_t month_days = (153 * (month - 3) + 2) / 5;
    /* 719468 is the same count for 1970-01-01. */
    return era_days + month_days + day - 1 - 719468;
}

uint64_t arch_wall_clock_seconds(void)
{
    /* The "updating" flag has a window in which it is not yet set; reading
     * twice until both reads agree closes it. */
    struct rtc_time time, check;
    do {
        read_registers(&time);
        read_registers(&check);
    } while (!same_time(&time, &check));

    uint8_t status_b = cmos_read(REG_STATUS_B);
    bool pm = (time.hours & HOURS_PM_FLAG) != 0;
    unsigned hours_raw = time.hours & (uint8_t)~HOURS_PM_FLAG;

    unsigned seconds = time.seconds, minutes = time.minutes, hours = hours_raw;
    unsigned day = time.day, month = time.month, year = time.year;
    if ((status_b & STATUS_B_BINARY) == 0) {
        seconds = from_bcd(time.seconds);
        minutes = from_bcd(time.minutes);
        hours = from_bcd((uint8_t)hours_raw);
        day = from_bcd(time.day);
        month = from_bcd(time.month);
        year = from_bcd(time.year);
    }
    if ((status_b & STATUS_B_24_HOUR) == 0) {
        hours = hours % 12 + (pm ? 12 : 0); /* 12 AM is hour 0 */
    }
    /* The register holds two digits; this OS was not written before 2000. */
    year += 2000;

    return days_since_epoch(year, month, day) * 86400ull + hours * 3600ull + minutes * 60ull + seconds;
}
