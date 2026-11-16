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
};

struct window {
    bool used;
    uint32_t id;
    struct client *client;
    enum gui_window_kind kind;
    unsigned flags; /* GUI_CREATE_* */

    /* The client's buffer, mapped here. */
    struct gfx_surface surface;
    void *mapping;
    size_t mapping_size;

    /* Position of the content area on screen and its size. */
    int x, y;
    int width, height;
    char title[GUI_TITLE_MAX + 1];

    bool visible;         /* false until the client's first commit */
    bool frame_requested; /* owes the client a GUI_MSG_FRAME */
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

void window_raise(struct window *window);
void window_focus(struct window *window);
void window_move(struct window *window, int x, int y);
/* Sends GUI_MSG_FRAME to every window that asked for one. */
void windows_send_frame_events(uint64_t time_ms);
bool windows_any_frame_requested(void);

/* ------------------------------------------------------- decorations */

#define TITLE_BAR_HEIGHT 28

enum hit_area {
    HIT_NONE,
    HIT_CONTENT,
    HIT_TITLE_BAR,
    HIT_CLOSE_BUTTON,
    HIT_MINIMIZE_BUTTON,
    HIT_ZOOM_BUTTON,
};

/* The whole area a window occupies on screen, decoration included. */
struct gfx_rect window_frame_rect(const struct window *window);
struct gfx_rect window_content_rect(const struct window *window);
enum hit_area window_hit_test(const struct window *window, int x, int y);
/* The topmost visible window at a point, and which part of it. */
struct window *window_at(int x, int y, enum hit_area *area);

void decor_init(void);
void decor_draw(struct gfx_surface *target, const struct window *window, bool focused);

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
/* A window is going away: forget any reference to it. */
void input_window_destroyed(struct window *window);
struct gfx_rect cursor_rect(void);
void cursor_draw(struct gfx_surface *target);

/* -------------------------------------------------------------- main */

extern bool option_test_mode; /* log what happens, quit on F12 */
void compositor_log(const char *format, ...) __attribute__((format(printf, 1, 2)));
void compositor_quit(void);

#endif
