#ifndef GUI_PROTOCOL_H
#define GUI_PROTOCOL_H

#include <stdint.h>

#include <saddev/input.h>

/*
 * The protocol between the window server (compositor) and its clients.
 *
 * A client connects to the named port GUI_SERVER_PORT and exchanges
 * fixed-size `struct gui_message` records over the resulting channel.
 * Window contents are not sent in messages: each window has a shared
 * memory buffer that the client draws into and the server reads.
 *
 * Applications normally use the wrapper in <gui/gui.h> instead of this.
 */

#define GUI_SERVER_PORT "saddev.compositor"
#define GUI_TITLE_MAX 63
#define GUI_APP_NAME_MAX 31
#define GUI_MAX_WINDOW_SIZE 4096 /* pixels, each direction */

enum gui_window_kind {
    GUI_WINDOW_NORMAL = 0, /* an application window with a title bar */
    GUI_WINDOW_DESKTOP,    /* below all others, no title bar (wallpaper) */
    GUI_WINDOW_PANEL,      /* above all others, no title bar (menu bar, dock) */
};

/* Flags for GUI_MSG_CREATE_WINDOW. */
#define GUI_CREATE_CENTERED    (1u << 0) /* ignore x and y */
#define GUI_CREATE_TRANSLUCENT (1u << 1) /* the buffer's alpha is used */
#define GUI_CREATE_BLUR_BEHIND (1u << 2) /* blur what shows through */
#define GUI_CREATE_NO_FOCUS    (1u << 3) /* never takes keyboard focus */

/* Flags for GUI_MSG_COMMIT. */
#define GUI_COMMIT_WANT_FRAME (1u << 0) /* send GUI_MSG_FRAME once shown */

enum gui_message_type {
    /* ---- client to server ---- */

    /* Attachment: a shared memory object of at least width * height * 4
     * bytes, rows of `width` 32-bit premultiplied ARGB pixels. Answered by
     * GUI_MSG_WINDOW_CREATED or GUI_MSG_ERROR. */
    GUI_MSG_CREATE_WINDOW = 1,
    GUI_MSG_DESTROY_WINDOW,
    /* "I have finished drawing this part of the buffer": the server shows
     * it at the next frame. The client must not draw again until it gets
     * GUI_MSG_FRAME, or the server may read a half-drawn buffer. */
    GUI_MSG_COMMIT,
    GUI_MSG_SET_TITLE,
    /* Replaces the buffer after a resize. Attachment and size as for
     * GUI_MSG_CREATE_WINDOW. */
    GUI_MSG_ATTACH_BUFFER,
    /* Names the application this connection belongs to, e.g. "Terminal".
     * Shown in the menu bar and used by the dock to group windows. */
    GUI_MSG_HELLO,
    /* Limits where the window reacts to the pointer (relative to its
     * content). Outside it, clicks go to whatever is below. For windows
     * that are partly transparent, like the dock. */
    GUI_MSG_SET_INPUT_REGION,

    /* The desktop shell (menu bar and dock) additionally uses: */
    GUI_MSG_SHELL_REGISTER,  /* "send me GUI_MSG_SHELL_WINDOW updates" */
    GUI_MSG_SHELL_ACTIVATE,  /* bring `window` to the front, restoring it if minimized */
    /* Where on screen `window` is represented in the dock: the target of
     * its minimize animation. */
    GUI_MSG_SHELL_ICON_RECT,

    /* ---- server to client ---- */
    GUI_MSG_WELCOME = 100,   /* first message: the screen size */
    GUI_MSG_WINDOW_CREATED,  /* `window` holds the new window's id */
    GUI_MSG_ERROR,
    GUI_MSG_FRAME,           /* the last commit is on screen */
    GUI_MSG_KEY,
    GUI_MSG_POINTER,
    GUI_MSG_FOCUS,
    GUI_MSG_CLOSE_REQUEST,   /* the user asked to close the window */
    GUI_MSG_RESIZE_REQUEST,  /* the user is resizing: attach a buffer of this size */
    /* To the shell: an application window appeared, changed or went away. */
    GUI_MSG_SHELL_WINDOW,
};

/* State bits in GUI_MSG_SHELL_WINDOW. A message without GUI_SHELL_EXISTS
 * means the window is gone. */
#define GUI_SHELL_EXISTS    (1u << 0)
#define GUI_SHELL_MINIMIZED (1u << 1)
#define GUI_SHELL_FOCUSED   (1u << 2)

enum gui_pointer_kind {
    GUI_POINTER_MOVE = 0,
    GUI_POINTER_DOWN,
    GUI_POINTER_UP,
    GUI_POINTER_SCROLL,
    GUI_POINTER_LEAVE, /* the pointer left the window */
};

struct gui_message {
    uint32_t type;   /* enum gui_message_type */
    uint32_t window; /* window id, where one applies */
    union {
        struct {
            uint32_t kind;  /* enum gui_window_kind */
            uint32_t flags; /* GUI_CREATE_* */
            int32_t x, y;   /* position of the content area on screen */
            uint32_t width, height;
            char title[GUI_TITLE_MAX + 1];
        } create;
        struct {
            int32_t x, y;
            uint32_t width, height;
            uint32_t flags; /* GUI_COMMIT_* */
        } commit;
        struct {
            char title[GUI_TITLE_MAX + 1];
        } title;
        struct {
            uint32_t width, height;
        } size; /* WELCOME, RESIZE_REQUEST, ATTACH_BUFFER */
        struct {
            int32_t code; /* an errno value */
        } error;
        struct {
            uint64_t time_ms;
        } frame;
        struct sd_key_event key;
        struct {
            int32_t x, y;     /* relative to the content area */
            uint32_t buttons; /* MOUSE_BUTTON_* held after this event */
            uint32_t kind;    /* enum gui_pointer_kind */
            int32_t wheel;
        } pointer;
        struct {
            uint32_t focused; /* 1 = gained, 0 = lost */
        } focus;
        struct {
            char app[GUI_APP_NAME_MAX + 1];
        } hello;
        struct {
            int32_t x, y;
            uint32_t width, height;
        } region; /* SET_INPUT_REGION, SHELL_ICON_RECT */
        struct {
            uint32_t state; /* GUI_SHELL_* */
            char app[GUI_APP_NAME_MAX + 1];
            char title[GUI_TITLE_MAX + 1];
        } shell_window;
    };
};

#endif
