/*
 * The menu bar: a translucent strip across the top of the screen with the
 * name of the active application on the left and the date and time on the
 * right.
 */
#include "desktop.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define BAR_TINT GFX_RGBA(246, 246, 250, 150) /* the "glass" colour over the blurred scene */
#define BAR_TEXT GFX_RGB(28, 28, 34)
#define SIDE_MARGIN 14
#define DEFAULT_APP_NAME "Saddev"
#define CONFIG_PATH "/etc/saddev.conf"

static struct gui_window *bar;
static char shown_app[GUI_APP_NAME_MAX + 1];
static uint64_t shown_minute = UINT64_MAX;
/* Local time is UTC plus this; read once from the configuration file. */
static int timezone_minutes;

/* Reads "timezone_minutes=<n>" from the configuration file, if present. */
static int read_timezone(void)
{
    char text[256];
    int descriptor = open(CONFIG_PATH);
    if (descriptor < 0) {
        return 0;
    }
    long size = read(descriptor, text, sizeof(text) - 1);
    close(descriptor);
    if (size <= 0) {
        return 0;
    }
    text[size] = '\0';
    const char *setting = strstr(text, "timezone_minutes=");
    return setting != NULL ? atoi(setting + strlen("timezone_minutes=")) : 0;
}

struct calendar {
    int year, month, day, weekday; /* weekday 0 = Sunday */
    int hour, minute;
};

/* Converts seconds since 1970 into a calendar date and time of day. */
static struct calendar calendar_from(uint64_t seconds)
{
    struct calendar result;
    uint64_t days = seconds / 86400;
    unsigned second_of_day = (unsigned)(seconds % 86400);
    result.hour = (int)(second_of_day / 3600);
    result.minute = (int)(second_of_day % 3600 / 60);
    result.weekday = (int)((days + 4) % 7); /* 1970-01-01 was a Thursday */

    /* Days to year/month/day, the inverse of the usual day-count formula,
     * with the year starting in March so the leap day comes last. */
    uint64_t shifted = days + 719468;
    uint64_t era = shifted / 146097;
    unsigned day_of_era = (unsigned)(shifted - era * 146097);
    unsigned year_of_era = (day_of_era - day_of_era / 1460 + day_of_era / 36524 - day_of_era / 146096) / 365;
    unsigned day_of_year = day_of_era - (365 * year_of_era + year_of_era / 4 - year_of_era / 100);
    unsigned month_index = (5 * day_of_year + 2) / 153; /* 0 = March */
    result.day = (int)(day_of_year - (153 * month_index + 2) / 5 + 1);
    result.month = (int)(month_index < 10 ? month_index + 3 : month_index - 9);
    result.year = (int)(year_of_era + era * 400) + (result.month <= 2 ? 1 : 0);
    return result;
}

static uint64_t local_seconds(void)
{
    return wall_time() + (uint64_t)((int64_t)timezone_minutes * 60);
}

/* The mark at the left end: an original, simple shape (a ring with a dot). */
static int draw_logo(struct gfx_surface *surface, int x)
{
    int center_y = MENU_BAR_HEIGHT / 2;
    gfx_fill_circle(surface, x + 7, center_y, 7, BAR_TEXT);
    gfx_fill_circle(surface, x + 7, center_y, 4, GFX_RGB(246, 246, 250));
    gfx_fill_circle(surface, x + 7, center_y, 2, BAR_TEXT);
    return x + 14;
}

static void draw(const char *app, uint64_t now)
{
    static const char *const weekdays[] = { "Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat" };
    static const char *const months[] = { "Jan", "Feb", "Mar", "Apr", "May", "Jun",
                                          "Jul", "Aug", "Sep", "Oct", "Nov", "Dec" };
    struct gfx_surface *surface = &bar->surface;
    int baseline = (MENU_BAR_HEIGHT + gfx_font_ascent(font_regular) - gfx_font_descent(font_regular)) / 2;

    gfx_clear(surface, gfx_surface_bounds(surface), BAR_TINT);

    int x = draw_logo(surface, SIDE_MARGIN) + 12;
    gfx_draw_text(surface, font_semibold, x, baseline, app, -1, BAR_TEXT);

    struct calendar when = calendar_from(now);
    char clock[48];
    snprintf(clock, sizeof(clock), "%s %d %s  %02d:%02d", weekdays[when.weekday], when.day,
             months[when.month - 1], when.hour, when.minute);
    int clock_width = gfx_text_width(font_regular, clock, -1);
    gfx_draw_text(surface, font_regular, surface->width - SIDE_MARGIN - clock_width, baseline, clock, -1, BAR_TEXT);

    gui_window_commit(bar, gfx_surface_bounds(surface), false);
}

void menubar_update(void)
{
    const char *app = model_focused_app();
    if (app == NULL || app[0] == '\0') {
        app = DEFAULT_APP_NAME;
    }
    uint64_t now = local_seconds();
    if (strcmp(app, shown_app) == 0 && now / 60 == shown_minute) {
        return;
    }
    if (strcmp(app, shown_app) != 0) {
        printf("desktop: active application is %s\n", app);
    }
    strlcpy(shown_app, app, sizeof(shown_app));
    shown_minute = now / 60;
    draw(app, now);
}

int64_t menubar_time_until_update(void)
{
    /* The clock shows minutes: wake up just after the next one begins. */
    return (int64_t)(60 - local_seconds() % 60) * 1000 + 50;
}

bool menubar_create(struct gui_connection *connection)
{
    timezone_minutes = read_timezone();
    bar = gui_window_create(connection, GUI_WINDOW_PANEL, 0, 0, gui_screen_width(connection), MENU_BAR_HEIGHT,
                            "Menu Bar", GUI_CREATE_TRANSLUCENT | GUI_CREATE_BLUR_BEHIND | GUI_CREATE_NO_FOCUS);
    if (bar == NULL) {
        return false;
    }
    menubar_update();
    return true;
}
