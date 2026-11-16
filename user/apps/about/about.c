/*
 * About: a small window with the system's name, version and a few live
 * figures.
 */
#include <gui/gui.h>
#include <gui/ui.h>
#include <saddev/version.h>
#include <stdio.h>
#include <unistd.h>

#define WIDTH 400
#define HEIGHT 330
#define REFRESH_MS 1000

static struct gui_window *window;

/* The system's mark, the same shape as in the menu bar, larger. */
static void draw_mark(struct gfx_surface *surface, int center_x, int center_y)
{
    gfx_fill_circle(surface, center_x, center_y, 34, GFX_RGB(40, 44, 70));
    gfx_fill_circle(surface, center_x, center_y, 21, UI_COLOR_WINDOW);
    gfx_fill_circle(surface, center_x, center_y, 10, UI_COLOR_ACCENT);
}

static void draw_row(struct gfx_surface *surface, int y, const char *label, const char *value)
{
    ui_text_right(surface, UI_FONT_BODY, gfx_rect_make(0, y, WIDTH / 2 - 8, 22), label, UI_COLOR_SECONDARY);
    ui_text(surface, UI_FONT_BODY, gfx_rect_make(WIDTH / 2 + 8, y, WIDTH / 2 - 24, 22), value, UI_COLOR_TEXT);
}

static void draw(struct gui_connection *connection)
{
    struct gfx_surface *surface = &window->surface;
    struct sd_sysinfo info = { 0 };
    char text[80];
    sysinfo(&info);

    gfx_clear(surface, gfx_surface_bounds(surface), UI_COLOR_WINDOW);
    draw_mark(surface, WIDTH / 2, 58);
    ui_text_centered(surface, UI_FONT_TITLE, gfx_rect_make(0, 102, WIDTH, 40), SADDEV_NAME, UI_COLOR_TEXT);
    ui_text_centered(surface, UI_FONT_BODY, gfx_rect_make(0, 142, WIDTH, 20), "Version " SADDEV_VERSION,
                     UI_COLOR_SECONDARY);
    gfx_fill_rect(surface, gfx_rect_make(40, 174, WIDTH - 80, 1), UI_COLOR_SEPARATOR);

    snprintf(text, sizeof(text), "%llu MiB free of %llu MiB", (unsigned long long)(info.memory_free >> 20),
             (unsigned long long)(info.memory_total >> 20));
    draw_row(surface, 186, "Memory", text);
    snprintf(text, sizeof(text), "%d \xC3\x97 %d", gui_screen_width(connection), gui_screen_height(connection));
    draw_row(surface, 208, "Display", text);
    unsigned long long seconds = info.uptime_ms / 1000;
    snprintf(text, sizeof(text), "%llu min %llu s", seconds / 60, seconds % 60);
    draw_row(surface, 230, "Uptime", text);
    snprintf(text, sizeof(text), "%u processes, %u threads", info.processes, info.threads);
    draw_row(surface, 252, "Running", text);

    ui_text_centered(surface, UI_FONT_SMALL, gfx_rect_make(0, 290, WIDTH, 18),
                     "An operating system written from scratch in C.", UI_COLOR_SECONDARY);
    ui_text_centered(surface, UI_FONT_SMALL, gfx_rect_make(0, 306, WIDTH, 18),
                     "Typefaces: Inter and JetBrains Mono (SIL Open Font License).", UI_COLOR_SECONDARY);
    gui_window_commit(window, gfx_surface_bounds(surface), false);
}

int main(void)
{
    struct gui_connection *connection = gui_connect("About");
    if (connection == NULL) {
        return 1;
    }
    window = gui_window_create(connection, GUI_WINDOW_NORMAL, 0, 0, WIDTH, HEIGHT, "About " SADDEV_NAME,
                               GUI_CREATE_CENTERED);
    if (window == NULL) {
        return 1;
    }
    draw(connection);
    printf("about: shown\n");

    for (;;) {
        struct gui_event event;
        int status = gui_next_event(connection, &event, REFRESH_MS);
        if (status < 0 || (status > 0 && event.type == GUI_MSG_CLOSE_REQUEST)) {
            break;
        }
        if (status == 0) {
            draw(connection); /* a second passed: refresh the figures */
        }
    }
    gui_disconnect(connection);
    return 0;
}
