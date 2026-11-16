#ifndef COMPOSITOR_H
#define COMPOSITOR_H

#include <stdbool.h>
#include <stdint.h>

#include <saddev/syscall.h>

#include <gui/gfx.h>
#include <gui/protocol.h>

/*
 * The window server. One process, one thread, one event loop (main.c):
 * it waits for input, client messages and the next frame time, and
 * redraws the parts of the screen that changed.
 *
 * All state below is used only from that loop, so nothing is locked.
 */

#define MAX_CLIENTS 32
#define MAX_WINDOWS 64

/* ----------------------------------------------------------- windows */

struct client {
    bool used;
    int channel;
    char app[GUI_APP_NAME_MAX + 1];
};

/* A client buffer mapped into the server. */
struct buffer {
    struct gfx_surface surface;
    void *mapping;
    size_t mapping_size;
};

struct window {
    bool used;
    uint32_t id;
    struct client *client;
    enum gui_window_kind kind;
    unsigned flags; /* GUI_CREATE_* */

    /* What is shown. Position of the content area on screen and its size. */
    struct buffer buffer;
    int x, y;
    int width, height;
    char title[GUI_TITLE_MAX + 1];

    bool visible;         /* false until the client's first commit */
    bool minimized;
    /* An animation is standing in for the window (see anim.c): it exists
     * and may have the focus, but is not drawn or hit by the pointer. */
    bool animation_hidden;
    /* Where the dock shows this window, as told by the shell: the place a
     * minimize animation flies to. */
    bool has_icon_rect;
    struct gfx_rect icon_rect;
    bool frame_requested; /* owes the client a GUI_MSG_FRAME */

    /* Where the pointer can hit the window, relative to the content; the
     * whole content if not set. */
    bool has_input_region;
    struct gfx_rect input_region;

    /*
     * Resizing takes two steps from the client: it attaches a new buffer,
     * then draws and commits. Until that commit the old buffer stays on
     * screen, so the user never sees an empty window.
     */
    bool has_pending_buffer;
    struct buffer pending_buffer;
    bool has_pending_position; /* move here when the new buffer is shown */
    int pending_x, pending_y;
    /* The size the user currently wants, and whether a request for some
     * size is still unanswered (only one is outstanding at a time). */
    int wanted_width, wanted_height;
    bool resize_outstanding;

    /* Zoom (maximize) toggles between the work area and this. */
    bool zoomed;
    struct gfx_rect unzoomed_rect;
};

/* Windows in drawing order, bottom first. */
extern struct window *window_stack[MAX_WINDOWS];
extern int window_stack_count;
extern struct window *focused_window;

void clients_init(void);
/* Accepts a pending connection on the server port. */
void clients_accept(int port);
/* Fills `descriptors` with every client channel; returns the count. */
int clients_poll_descriptors(struct sd_pollfd *descriptors, struct client **owners, int capacity);
/* Reads and handles everything a client has sent. */
void client_service(struct client *client);
void client_send(struct client *client, const struct gui_message *message);

/* True if the window is part of the scene: visible and not minimized. */
bool window_is_shown(const struct window *window);
/* True if it is actually drawn right now (not replaced by an animation). */
bool window_is_drawn(const struct window *window);
void window_raise(struct window *window);
void window_focus(struct window *window);
void window_move(struct window *window, int x, int y);
void window_minimize(struct window *window);
/* Asks the client for a new size; the window changes once it complies. */
void window_request_size(struct window *window, int width, int height);
void window_toggle_zoom(struct window *window);
/* The part of the screen not reserved by panels (menu bar, dock). */
struct gfx_rect work_area(void);
/* Sends GUI_MSG_FRAME to every window that asked for one. */
void windows_send_frame_events(uint64_t time_ms);
bool windows_any_frame_requested(void);

/* ------------------------------------------------------- decorations */

#define TITLE_BAR_HEIGHT 28
#define WINDOW_CORNER_RADIUS 10
#define WINDOW_MIN_WIDTH 200
#define WINDOW_MIN_HEIGHT 120

enum hit_area {
    HIT_NONE,
    HIT_CONTENT,
    HIT_TITLE_BAR,
    HIT_CLOSE_BUTTON,
    HIT_MINIMIZE_BUTTON,
    HIT_ZOOM_BUTTON,
    HIT_RESIZE, /* see resize_edges_at() */
};

#define RESIZE_RIGHT  (1u << 0)
#define RESIZE_BOTTOM (1u << 1)

/* The title bar plus the content. */
struct gfx_rect window_frame_rect(const struct window *window);
struct gfx_rect window_content_rect(const struct window *window);
/* Everything the window draws on screen, shadow included: the area to
 * redraw when the window changes. */
struct gfx_rect window_visual_rect(const struct window *window);
enum hit_area window_hit_test(const struct window *window, int x, int y);
/* Which edges a resize started at (x, y) would move: RESIZE_* bits. */
unsigned resize_edges_at(const struct window *window, int x, int y);
/* The topmost shown window at a point, and which part of it. */
struct window *window_at(int x, int y, enum hit_area *area);

void decor_init(void);
/* Draws the title bar of a window and, if asked, its shadow. */
void decor_draw(struct gfx_surface *target, const struct window *window, bool focused, bool with_shadow);
/* Copies a window's content with its bottom corners rounded off. */
void decor_draw_content(struct gfx_surface *target, const struct window *window);

/* ------------------------------------------------------------ damage */

/* Records that part of the screen must be redrawn. */
void damage_add(struct gfx_rect rect);
bool damage_pending(void);
/* Moves the recorded rectangles to `out` (at most `capacity`); returns how
 * many there were. The record is empty afterwards. */
int damage_take(struct gfx_rect *out, int capacity);
#define DAMAGE_MAX_RECTS 16

/* --------------------------------------------------------- rendering */

extern struct gfx_surface screen;      /* the display */
extern struct gfx_surface back_buffer; /* where frames are composed */

bool render_init(void);
/* Redraws all damaged areas and copies them to the display. */
void render_frame(void);

/* ------------------------------------------------------------- input */

extern int pointer_x, pointer_y;

bool input_init(void);
/* Descriptors for the main loop to poll. */
int input_mouse_descriptor(void);
int input_keyboard_descriptor(void);
void input_handle_mouse(void);
void input_handle_keyboard(void);
/* A window is going away or being hidden: forget any reference to it. */
void input_window_gone(struct window *window);
struct gfx_rect cursor_rect(void);
void cursor_draw(struct gfx_surface *target);

/* -------------------------------------------------------- animations */

void animations_set_scale(unsigned scale);
bool animations_active(void);
/* Moves every running animation to where it should be at `now_ns`. */
void animations_update(uint64_t now_ns);
void animations_draw(struct gfx_surface *target);
void animate_open(struct window *window);
/* Call before the window is destroyed: its picture is taken now. */
void animate_close(struct window *window);
/* Call before the window is marked minimized, for the same reason. */
void animate_minimize(struct window *window);
void animate_restore(struct window *window);
void animations_window_gone(struct window *window);

/* ------------------------------------------------------------- shell */

/* Tells the desktop shell, if one is connected, about a window's state. */
void shell_notify(const struct window *window);

/* -------------------------------------------------------------- main */

extern bool option_test_mode; /* log what happens, quit on F12 */
void compositor_log(const char *format, ...) __attribute__((format(printf, 1, 2)));
void compositor_quit(void);

#endif
