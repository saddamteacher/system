/*
 * Mouse and keyboard: the pointer, clicking, dragging windows, and routing
 * events to clients.
 */
#include "compositor.h"

#include <unistd.h>

#define CURSOR_WIDTH 12
#define CURSOR_HEIGHT 19

/* '#' is the black outline, '.' the white interior; the hot spot is the
 * top-left tip. */
static const char *const cursor_shape[CURSOR_HEIGHT] = {
    "#           ",
    "##          ",
    "#.#         ",
    "#..#        ",
    "#...#       ",
    "#....#      ",
    "#.....#     ",
    "#......#    ",
    "#.......#   ",
    "#........#  ",
    "#.........# ",
    "#......#####",
    "#...#..#    ",
    "#..# #..#   ",
    "#.#  #..#   ",
    "##    #..#  ",
    "#     #..#  ",
    "       ##   ",
    "            ",
};

int pointer_x, pointer_y;

static int mouse_descriptor = -1;
static int keyboard_descriptor = -1;
static unsigned pointer_buttons;

/* A window being dragged by its title bar, and where on it the pointer
 * grabbed it. */
static struct window *dragged_window;
static int drag_offset_x, drag_offset_y;
/* While a button is held, pointer events keep going to the window where
 * the press happened, even if the pointer leaves it. Otherwise a drag
 * inside an application (selecting text, moving a slider) would break at
 * the window's edge. */
static struct window *grabbed_window;
/* The window that last received a pointer event, to tell it when the
 * pointer leaves. */
static struct window *hovered_window;
/* A title bar button that was pressed and not yet released. */
static struct window *pressed_button_window;
static enum hit_area pressed_button;
/* A window being resized by an edge: which edges, and the pointer position
 * and window size when it started. */
static struct window *resized_window;
static unsigned resize_edges;
static int resize_start_x, resize_start_y;
static int resize_start_width, resize_start_height;

bool input_init(void)
{
    mouse_descriptor = open("/dev/mouse");
    keyboard_descriptor = open("/dev/keyboard");
    pointer_x = screen.width / 2;
    pointer_y = screen.height / 2;
    return mouse_descriptor >= 0 && keyboard_descriptor >= 0;
}

int input_mouse_descriptor(void)
{
    return mouse_descriptor;
}

int input_keyboard_descriptor(void)
{
    return keyboard_descriptor;
}

struct gfx_rect cursor_rect(void)
{
    return gfx_rect_make(pointer_x, pointer_y, CURSOR_WIDTH, CURSOR_HEIGHT);
}

void cursor_draw(struct gfx_surface *target)
{
    if (gfx_rect_is_empty(gfx_rect_intersect(cursor_rect(), target->clip))) {
        return;
    }
    for (int row = 0; row < CURSOR_HEIGHT; row++) {
        for (int column = 0; column < CURSOR_WIDTH; column++) {
            char cell = cursor_shape[row][column];
            if (cell != ' ') {
                gfx_fill_rect(target, gfx_rect_make(pointer_x + column, pointer_y + row, 1, 1),
                              cell == '#' ? GFX_BLACK : GFX_WHITE);
            }
        }
    }
}

void input_window_gone(struct window *window)
{
    if (resized_window == window) {
        resized_window = NULL;
    }
    if (dragged_window == window) {
        dragged_window = NULL;
    }
    if (grabbed_window == window) {
        grabbed_window = NULL;
    }
    if (hovered_window == window) {
        hovered_window = NULL;
    }
    if (pressed_button_window == window) {
        pressed_button_window = NULL;
    }
}

static void send_pointer(struct window *window, enum gui_pointer_kind kind, int wheel)
{
    struct gui_message message = { .type = GUI_MSG_POINTER, .window = window->id };
    message.pointer.x = pointer_x - window->x;
    message.pointer.y = pointer_y - window->y;
    message.pointer.buttons = pointer_buttons;
    message.pointer.kind = kind;
    message.pointer.wheel = wheel;
    client_send(window->client, &message);
}

/* Sends a pointer event to whichever window should get it, and a "leave"
 * to the previous one if that changed. */
static void route_pointer(enum gui_pointer_kind kind, int wheel)
{
    struct window *target = grabbed_window;
    if (target == NULL) {
        enum hit_area area;
        struct window *under = window_at(pointer_x, pointer_y, &area);
        target = area == HIT_CONTENT ? under : NULL;
    }
    if (hovered_window != NULL && hovered_window != target) {
        send_pointer(hovered_window, GUI_POINTER_LEAVE, 0);
    }
    hovered_window = target;
    if (target != NULL) {
        send_pointer(target, kind, wheel);
    }
}

static void on_title_bar_button(struct window *window, enum hit_area button)
{
    switch (button) {
    case HIT_CLOSE_BUTTON: {
        /* Ask, do not force: the application may want to save first. */
        struct gui_message message = { .type = GUI_MSG_CLOSE_REQUEST, .window = window->id };
        client_send(window->client, &message);
        break;
    }
    case HIT_MINIMIZE_BUTTON:
        window_minimize(window);
        break;
    case HIT_ZOOM_BUTTON:
        window_toggle_zoom(window);
        break;
    default:
        break;
    }
}

static void on_button_down(void)
{
    enum hit_area area;
    struct window *window = window_at(pointer_x, pointer_y, &area);
    compositor_log("pointer down at %d %d", pointer_x, pointer_y);
    if (window == NULL) {
        return;
    }
    if (window->kind == GUI_WINDOW_NORMAL) {
        window_raise(window);
        window_focus(window);
        compositor_log("window %u raised", window->id);
    }

    switch (area) {
    case HIT_TITLE_BAR:
        /* A zoomed window fills the work area; it is not dragged about. */
        if (!window->zoomed) {
            dragged_window = window;
            drag_offset_x = pointer_x - window->x;
            drag_offset_y = pointer_y - window->y;
        }
        break;
    case HIT_RESIZE:
        resized_window = window;
        resize_edges = resize_edges_at(window, pointer_x, pointer_y);
        resize_start_x = pointer_x;
        resize_start_y = pointer_y;
        resize_start_width = window->width;
        resize_start_height = window->height;
        break;
    case HIT_CLOSE_BUTTON:
    case HIT_MINIMIZE_BUTTON:
    case HIT_ZOOM_BUTTON:
        /* Buttons act on release, and only if the pointer is still on
         * them: that is the chance to change one's mind. */
        pressed_button_window = window;
        pressed_button = area;
        break;
    case HIT_CONTENT:
        grabbed_window = window;
        route_pointer(GUI_POINTER_DOWN, 0);
        break;
    default:
        break;
    }
}

static void on_button_up(void)
{
    compositor_log("pointer up at %d %d", pointer_x, pointer_y);
    if (dragged_window != NULL) {
        compositor_log("window %u moved to %d %d", dragged_window->id, dragged_window->x, dragged_window->y);
        dragged_window = NULL;
    }
    resized_window = NULL;
    if (pressed_button_window != NULL) {
        enum hit_area area;
        if (window_at(pointer_x, pointer_y, &area) == pressed_button_window && area == pressed_button) {
            on_title_bar_button(pressed_button_window, pressed_button);
        }
        pressed_button_window = NULL;
    }
    if (grabbed_window != NULL) {
        route_pointer(GUI_POINTER_UP, 0);
        grabbed_window = NULL;
    }
}

static int clamp(int value, int low, int high)
{
    return value < low ? low : (value > high ? high : value);
}

static void on_motion(int dx, int dy)
{
    if (dx == 0 && dy == 0) {
        return;
    }
    damage_add(cursor_rect());
    pointer_x = clamp(pointer_x + dx, 0, screen.width - 1);
    pointer_y = clamp(pointer_y + dy, 0, screen.height - 1);
    damage_add(cursor_rect());

    if (dragged_window != NULL) {
        /* The title bar may not slide under the menu bar. */
        int top = work_area().y + TITLE_BAR_HEIGHT;
        int y = pointer_y - drag_offset_y;
        if (y < top) {
            y = top;
        }
        window_move(dragged_window, pointer_x - drag_offset_x, y);
    } else if (resized_window != NULL) {
        int width = resize_start_width, height = resize_start_height;
        if (resize_edges & RESIZE_RIGHT) {
            width += pointer_x - resize_start_x;
        }
        if (resize_edges & RESIZE_BOTTOM) {
            height += pointer_y - resize_start_y;
        }
        window_request_size(resized_window, width, height);
    } else {
        route_pointer(GUI_POINTER_MOVE, 0);
    }
}

void input_handle_mouse(void)
{
    struct sd_mouse_event events[32];
    long size = read(mouse_descriptor, events, sizeof(events));

    for (long i = 0; i < size / (long)sizeof(events[0]); i++) {
        on_motion(events[i].dx, events[i].dy);

        unsigned changed = pointer_buttons ^ events[i].buttons;
        pointer_buttons = events[i].buttons;
        if (changed & MOUSE_BUTTON_LEFT) {
            if (pointer_buttons & MOUSE_BUTTON_LEFT) {
                on_button_down();
            } else {
                on_button_up();
            }
        }
        if (events[i].wheel != 0) {
            route_pointer(GUI_POINTER_SCROLL, events[i].wheel);
        }
    }
}

void input_handle_keyboard(void)
{
    struct sd_key_event events[16];
    long size = read(keyboard_descriptor, events, sizeof(events));

    for (long i = 0; i < size / (long)sizeof(events[0]); i++) {
        if (option_test_mode && events[i].pressed && events[i].code == KEY_F12) {
            compositor_quit();
            return;
        }
        if (focused_window != NULL) {
            struct gui_message message = { .type = GUI_MSG_KEY, .window = focused_window->id };
            message.key = events[i];
            client_send(focused_window->client, &message);
        }
    }
}
