#ifndef GUI_GUI_H
#define GUI_GUI_H

#include <stdbool.h>
#include <stdint.h>

#include <gui/gfx.h>
#include <gui/protocol.h>

/*
 * Client side of the window system: connect to the window server, create
 * windows, draw into them, receive input.
 *
 * A window's content is the `surface` in its struct. The usual cycle is:
 * draw into the surface, call gui_window_commit(), wait for events.
 */

struct gui_connection;

struct gui_window {
    struct gui_connection *connection;
    uint32_t id;
    struct gfx_surface surface; /* draw here */
    int width, height;
    /* True between a commit that asked for a frame event and that event.
     * While it is set the server may be reading the surface: do not draw. */
    bool frame_pending;
    void *user_data; /* for the application; not used by the library */

    /* Private. */
    void *mapping;
    size_t mapping_size;
    struct gui_window *next;
};

struct gui_event {
    uint32_t type;             /* GUI_MSG_KEY, _POINTER, _FOCUS, _FRAME,
                                  _CLOSE_REQUEST or _RESIZE_REQUEST */
    struct gui_window *window; /* NULL if the window no longer exists */
    union {
        struct sd_key_event key;
        struct {
            int x, y;
            unsigned buttons;
            unsigned kind; /* enum gui_pointer_kind */
            int wheel;
        } pointer;
        bool focused;
        struct {
            int width, height;
        } resize;
        uint64_t frame_time_ms;
    };
};

/* Connects to the window server; NULL if it is not running. */
struct gui_connection *gui_connect(void);
void gui_disconnect(struct gui_connection *connection);

int gui_screen_width(const struct gui_connection *connection);
int gui_screen_height(const struct gui_connection *connection);
/* The channel descriptor, for programs that poll several things at once. */
int gui_connection_descriptor(const struct gui_connection *connection);

/*
 * Creates a window whose content area is width x height pixels. `flags`
 * are GUI_CREATE_*; x and y are ignored with GUI_CREATE_CENTERED. Returns
 * NULL on failure. The surface starts fully transparent (for a normal
 * window: black) and nothing is shown until the first commit.
 */
struct gui_window *gui_window_create(struct gui_connection *connection, enum gui_window_kind kind, int x, int y,
                                     int width, int height, const char *title, unsigned flags);
void gui_window_destroy(struct gui_window *window);
void gui_window_set_title(struct gui_window *window, const char *title);

/*
 * Tells the server that `area` of the surface is ready to be shown. With
 * `want_frame`, the server answers with a GUI_MSG_FRAME event once it is on
 * screen; an animation draws its next frame when that arrives, which paces
 * it to the display and keeps the server from reading a half-drawn surface.
 */
void gui_window_commit(struct gui_window *window, struct gfx_rect area, bool want_frame);

/* Replaces the surface with a new one of the given size, in response to a
 * GUI_MSG_RESIZE_REQUEST. The new surface starts transparent; redraw and
 * commit. Returns false (and keeps the old surface) on failure. */
bool gui_window_resize(struct gui_window *window, int width, int height);

/*
 * Waits for the next event. Returns 1 with *event filled in, 0 if
 * `timeout_ms` passed without one (negative: wait forever), -1 if the
 * server is gone.
 */
int gui_next_event(struct gui_connection *connection, struct gui_event *event, int64_t timeout_ms);

#endif
