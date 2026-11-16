/*
 * The desktop shell's event loop, and its model of which application
 * windows exist.
 */
#include "desktop.h"

#include <ipc.h>
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

/* Handles everything the window server has sent. Returns false if the
 * connection is gone. */
static bool drain_window_events(struct gui_connection *connection)
{
    struct gui_event event;
    int status;
    while ((status = gui_next_event(connection, &event, 0)) > 0) {
        if (event.type == GUI_MSG_SHELL_WINDOW) {
            model_update(&event);
            dock_update();
        } else if (event.type == GUI_MSG_POINTER && dock_owns(event.window)) {
            dock_handle_pointer(&event);
        } else if (event.type == GUI_MSG_FRAME && dock_owns(event.window)) {
            dock_handle_frame();
        }
    }
    return status == 0;
}

static void apply_settings(uint32_t previous_wallpaper)
{
    if (shell_settings.wallpaper != previous_wallpaper) {
        wallpaper_apply();
    }
    menubar_invalidate();
    dock_update();
}

/* usage: desktop [--demo]   (--demo: test programs in the dock) */
int main(int argc, char **argv)
{
    bool demo = argc > 1 && strcmp(argv[1], "--demo") == 0;

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
    if (!settings_init()) {
        printf("desktop: cannot open the settings port; settings stay at their defaults\n");
    }
    if (!wallpaper_create(connection) || !menubar_create(connection) || !dock_create(connection, demo)) {
        printf("desktop: cannot create the desktop windows\n");
        return 1;
    }
    /* From now on the server reports every application window to us. */
    gui_shell_register(connection);
    printf("desktop: ready\n");

    for (;;) {
        /* Wait for the window server, a settings client, or the clock. */
        struct sd_pollfd sources[8];
        sources[0] = (struct sd_pollfd){ .fd = gui_connection_descriptor(connection), .events = SD_POLL_IN };
        int settings_count = settings_poll_descriptors(&sources[1], 7);
        poll_fds(sources, (size_t)(1 + settings_count), menubar_time_until_update());

        uint32_t wallpaper_before = shell_settings.wallpaper;
        if (settings_service(&sources[1], settings_count)) {
            apply_settings(wallpaper_before);
        }
        if (!drain_window_events(connection)) {
            break; /* the window server is gone */
        }
        /* Cheap when nothing changed: it compares before it draws. */
        menubar_update();
    }
    return 0;
}
