/*
 * Settings: changes how the desktop looks and behaves.
 *
 * The settings belong to the desktop shell; this program is a remote
 * control for them. It connects to the shell's settings port, receives the
 * current values, and sends back a changed copy whenever the user changes
 * something. The shell applies it immediately.
 */
#include <gui/gui.h>
#include <gui/shell_settings.h>
#include <gui/ui.h>
#include <ipc.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#define WIDTH 440
#define HEIGHT 290
#define ROW_HEIGHT 48
#define MARGIN 24
#define FIRST_ROW_Y 20
#define TIMEZONE_STEP_MINUTES 30

enum row { ROW_MAGNIFICATION, ROW_CLOCK, ROW_TIMEZONE, ROW_WALLPAPER, ROW_COUNT };

static struct gui_window *window;
static struct shell_settings settings;
static int shell_channel = -1;

static struct gfx_rect row_rect(enum row row)
{
    return gfx_rect_make(MARGIN, FIRST_ROW_Y + (int)row * ROW_HEIGHT, WIDTH - 2 * MARGIN, ROW_HEIGHT);
}

/* The control at the right end of each row. */
static struct gfx_rect switch_rect(enum row row)
{
    struct gfx_rect area = row_rect(row);
    return gfx_rect_make(area.x + area.width - UI_SWITCH_WIDTH, area.y + (ROW_HEIGHT - UI_SWITCH_HEIGHT) / 2,
                         UI_SWITCH_WIDTH, UI_SWITCH_HEIGHT);
}

static struct gfx_rect stepper_button(bool plus)
{
    struct gfx_rect area = row_rect(ROW_TIMEZONE);
    return gfx_rect_make(area.x + area.width - (plus ? 28 : 150), area.y + (ROW_HEIGHT - 26) / 2, 28, 26);
}

static struct gfx_rect wallpaper_control(void)
{
    struct gfx_rect area = row_rect(ROW_WALLPAPER);
    return gfx_rect_make(area.x + area.width - 230, area.y + (ROW_HEIGHT - 28) / 2, 230, 28);
}

static void draw(void)
{
    static const char *const labels[ROW_COUNT] = { "Magnify dock icons", "24-hour clock", "Time zone", "Wallpaper" };
    static const char *const wallpapers[SHELL_WALLPAPER_COUNT] = { "Dusk", "Ocean", "Forest" };
    struct gfx_surface *surface = &window->surface;

    gfx_clear(surface, gfx_surface_bounds(surface), UI_COLOR_WINDOW);
    gfx_fill_rounded_rect(surface, gfx_rect_make(MARGIN - 12, FIRST_ROW_Y - 4, WIDTH - 2 * MARGIN + 24,
                                                 ROW_COUNT * ROW_HEIGHT + 8), 10, UI_COLOR_PANEL);
    for (int row = 0; row < ROW_COUNT; row++) {
        struct gfx_rect area = row_rect((enum row)row);
        ui_text(surface, UI_FONT_BODY, area, labels[row], UI_COLOR_TEXT);
        if (row > 0) {
            gfx_fill_rect(surface, gfx_rect_make(area.x, area.y, area.width, 1), UI_COLOR_SEPARATOR);
        }
    }
    ui_switch(surface, switch_rect(ROW_MAGNIFICATION), settings.dock_magnification != 0);
    ui_switch(surface, switch_rect(ROW_CLOCK), settings.clock_24_hour != 0);

    ui_button(surface, stepper_button(false), "\xE2\x80\x93", false, false);
    ui_button(surface, stepper_button(true), "+", false, false);
    char zone[32];
    int minutes = abs(settings.timezone_minutes);
    snprintf(zone, sizeof(zone), "UTC%c%d:%02d", settings.timezone_minutes < 0 ? '-' : '+', minutes / 60,
             minutes % 60);
    struct gfx_rect minus = stepper_button(false), plus = stepper_button(true);
    ui_text_centered(surface, UI_FONT_MEDIUM,
                     gfx_rect_make(minus.x + minus.width, minus.y, plus.x - minus.x - minus.width, minus.height),
                     zone, UI_COLOR_TEXT);

    ui_segmented(surface, wallpaper_control(), wallpapers, SHELL_WALLPAPER_COUNT, (int)settings.wallpaper);

    ui_text_centered(surface, UI_FONT_SMALL, gfx_rect_make(0, HEIGHT - 44, WIDTH, 18),
                     shell_channel >= 0 ? "Changes take effect immediately and last until restart."
                                        : "The desktop is not running: nothing to configure.",
                     UI_COLOR_SECONDARY);
    gui_window_commit(window, gfx_surface_bounds(surface), false);
}

static void send_settings(void)
{
    if (shell_channel >= 0) {
        msg_send(shell_channel, &settings, sizeof(settings), SD_NO_HANDLE);
    }
}

/* Returns true if a setting changed. */
static bool handle_click(int x, int y)
{
    if (gfx_rect_contains(switch_rect(ROW_MAGNIFICATION), x, y)) {
        settings.dock_magnification = !settings.dock_magnification;
    } else if (gfx_rect_contains(switch_rect(ROW_CLOCK), x, y)) {
        settings.clock_24_hour = !settings.clock_24_hour;
    } else if (gfx_rect_contains(stepper_button(false), x, y)) {
        if (settings.timezone_minutes - TIMEZONE_STEP_MINUTES >= SHELL_TIMEZONE_MIN) {
            settings.timezone_minutes -= TIMEZONE_STEP_MINUTES;
        }
    } else if (gfx_rect_contains(stepper_button(true), x, y)) {
        if (settings.timezone_minutes + TIMEZONE_STEP_MINUTES <= SHELL_TIMEZONE_MAX) {
            settings.timezone_minutes += TIMEZONE_STEP_MINUTES;
        }
    } else if (gfx_rect_contains(wallpaper_control(), x, y)) {
        for (int i = 0; i < SHELL_WALLPAPER_COUNT; i++) {
            if (gfx_rect_contains(ui_segment_rect(wallpaper_control(), SHELL_WALLPAPER_COUNT, i), x, y)) {
                settings.wallpaper = (uint32_t)i;
            }
        }
    } else {
        return false;
    }
    return true;
}

/* Connects to the shell and reads the settings it starts us with. */
static void connect_to_shell(void)
{
    int channel = port_connect(SHELL_SETTINGS_PORT);
    if (channel < 0) {
        return;
    }
    struct sd_pollfd reply = { .fd = channel, .events = SD_POLL_IN };
    if (poll_fds(&reply, 1, 2000) > 0 && msg_recv(channel, &settings, sizeof(settings), NULL) == sizeof(settings)) {
        shell_channel = channel;
    } else {
        close(channel);
    }
}

int main(void)
{
    struct gui_connection *connection = gui_connect("Settings");
    if (connection == NULL) {
        return 1;
    }
    window = gui_window_create(connection, GUI_WINDOW_NORMAL, 0, 0, WIDTH, HEIGHT, "Settings", GUI_CREATE_CENTERED);
    if (window == NULL) {
        return 1;
    }
    connect_to_shell();
    draw();
    printf("settings: shown, %s\n", shell_channel >= 0 ? "connected to the desktop" : "no desktop");

    for (;;) {
        struct gui_event event;
        if (gui_next_event(connection, &event, -1) < 0 || event.type == GUI_MSG_CLOSE_REQUEST) {
            break;
        }
        if (event.type == GUI_MSG_POINTER && event.pointer.kind == GUI_POINTER_UP
            && handle_click(event.pointer.x, event.pointer.y)) {
            send_settings();
            draw();
        }
    }
    gui_disconnect(connection);
    return 0;
}
