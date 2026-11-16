/*
 * The desktop shell's event loop, and its model of which application
 * windows exist.
 */
#include "desktop.h"

#include <stdio.h>
#include <string.h>

struct tracked_window tracked_windows[MAX_TRACKED_WINDOWS];
int tracked_window_count;
struct gfx_font *font_regular;
struct gfx_font *font_semibold;

static int find_tracked(uint32_t id)
{
    for (int i = 0; i < tracked_window_count; i++) {
        if (tracked_windows[i].id == id) {
            return i;
        }
    }
    return -1;
}

void model_update(const struct gui_event *event)
{
    int index = find_tracked(event->shell_window.id);

    if ((event->shell_window.state & GUI_SHELL_EXISTS) == 0) {
        if (index >= 0) {
            tracked_windows[index] = tracked_windows[--tracked_window_count];
        }
        return;
    }
    if (index < 0) {
        if (tracked_window_count == MAX_TRACKED_WINDOWS) {
            return;
        }
        index = tracked_window_count++;
    }
    struct tracked_window *window = &tracked_windows[index];
    window->id = event->shell_window.id;
    window->state = event->shell_window.state;
    strlcpy(window->app, event->shell_window.app, sizeof(window->app));
    strlcpy(window->title, event->shell_window.title, sizeof(window->title));
}

const char *model_focused_app(void)
{
    for (int i = 0; i < tracked_window_count; i++) {
        if (tracked_windows[i].state & GUI_SHELL_FOCUSED) {
            return tracked_windows[i].app;
        }
    }
    return NULL;
}

int main(void)
{
    struct gui_connection *connection = gui_connect("Desktop");
    if (connection == NULL) {
        printf("desktop: no window server\n");
        return 1;
    }
    font_regular = gfx_font_load("/fonts/Inter-Regular-13.sdf");
    font_semibold = gfx_font_load("/fonts/Inter-SemiBold-13.sdf");
    if (font_regular == NULL || font_semibold == NULL || !icons_init()) {
        printf("desktop: cannot load fonts or build icons\n");
        return 1;
    }
    if (!wallpaper_create(connection) || !menubar_create(connection) || !dock_create(connection)) {
        printf("desktop: cannot create the desktop windows\n");
        return 1;
    }
    /* From now on the server reports every application window to us. */
    gui_shell_register(connection);
    printf("desktop: ready\n");

    for (;;) {
        struct gui_event event;
        int status = gui_next_event(connection, &event, menubar_time_until_update());
        if (status < 0) {
            break; /* the window server is gone */
        }
        if (status > 0) {
            if (event.type == GUI_MSG_SHELL_WINDOW) {
                model_update(&event);
                dock_update();
            } else if (event.type == GUI_MSG_POINTER && dock_owns(event.window)) {
                dock_handle_pointer(&event);
            }
        }
        /* Cheap when nothing changed: it compares before it draws. */
        menubar_update();
    }
    return 0;
}
