/*
 * The dock: a translucent shelf of application icons, centred at the
 * bottom of the screen. Clicking an icon starts the application, or brings
 * its window forward if it is already running.
 */
#include "desktop.h"

#include <stdio.h>
#include <string.h>
#include <unistd.h>

#define ICON_SIZE 48
#define ICON_GAP 14
#define SHELF_PADDING 12
#define SHELF_HEIGHT (ICON_SIZE + 2 * 10)
#define SHELF_RADIUS 20
#define SHELF_BOTTOM_MARGIN 8
/* The window is taller than the shelf: icons will grow above it. */
#define WINDOW_HEIGHT 132
#define INDICATOR_RADIUS 2

#define SHELF_TINT   GFX_RGBA(250, 250, 252, 105)
#define SHELF_BORDER GFX_RGBA(255, 255, 255, 90)
#define INDICATOR_COLOR GFX_RGBA(30, 30, 36, 200)

struct dock_item {
    const char *app;       /* application name its windows report */
    enum icon_kind icon;
    char *const *command;  /* how to start it */
};

static char *const demo_alpha[] = { "/test/guidemo", "Alpha", "180", "150", "0", NULL };
static char *const demo_beta[] = { "/test/guidemo", "Beta", "460", "260", "1", NULL };
static char *const demo_gamma[] = { "/test/guidemo", "Gamma", "760", "170", "2", NULL };

static const struct dock_item items[] = {
    { "Alpha", ICON_DEMO_BLUE, demo_alpha },
    { "Beta", ICON_DEMO_ORANGE, demo_beta },
    { "Gamma", ICON_DEMO_GREEN, demo_gamma },
};

#define ITEM_COUNT ((int)(sizeof(items) / sizeof(items[0])))

static struct gui_window *dock;
static struct gfx_rect shelf; /* within the dock window */
static int pressed_item = -1;

static struct gfx_rect item_rect(int index)
{
    return gfx_rect_make(shelf.x + SHELF_PADDING + index * (ICON_SIZE + ICON_GAP),
                         shelf.y + (shelf.height - ICON_SIZE) / 2 - 2, ICON_SIZE, ICON_SIZE);
}

static int item_at(int x, int y)
{
    for (int i = 0; i < ITEM_COUNT; i++) {
        /* The gap between icons belongs to the nearer one, and the whole
         * height of the shelf counts: easier to hit. */
        struct gfx_rect rect = item_rect(i);
        if (x >= rect.x - ICON_GAP / 2 && x < rect.x + rect.width + ICON_GAP / 2 && y >= shelf.y
            && y < shelf.y + shelf.height) {
            return i;
        }
    }
    return -1;
}

/* An application's windows, by the name its windows carry. */
static const struct tracked_window *window_of(const char *app, bool prefer_hidden)
{
    const struct tracked_window *found = NULL;
    for (int i = 0; i < tracked_window_count; i++) {
        if (strcmp(tracked_windows[i].app, app) != 0) {
            continue;
        }
        if (found == NULL || (prefer_hidden && (tracked_windows[i].state & GUI_SHELL_MINIMIZED))) {
            found = &tracked_windows[i];
        }
    }
    return found;
}

void dock_update(void)
{
    struct gfx_surface *surface = &dock->surface;
    gfx_clear(surface, gfx_surface_bounds(surface), GFX_TRANSPARENT);

    /* A bright rim under the tint reads as the edge of a glass pane. */
    gfx_fill_rounded_rect(surface, shelf, SHELF_RADIUS, SHELF_BORDER);
    gfx_fill_rounded_rect(surface, gfx_rect_inset(shelf, -1), SHELF_RADIUS - 1, SHELF_TINT);

    for (int i = 0; i < ITEM_COUNT; i++) {
        struct gfx_rect rect = item_rect(i);
        icon_draw(surface, items[i].icon, rect.x, rect.y, rect.width);
        if (window_of(items[i].app, false) != NULL) {
            /* A dot marks applications that have a window open. */
            gfx_fill_circle(surface, rect.x + rect.width / 2, shelf.y + shelf.height - 5, INDICATOR_RADIUS,
                            INDICATOR_COLOR);
        }
    }
    gui_window_commit(dock, gfx_surface_bounds(surface), false);
}

static void activate(int index)
{
    const struct dock_item *item = &items[index];
    /* A minimized window is the one the user most likely wants back. */
    const struct tracked_window *window = window_of(item->app, true);
    if (window != NULL) {
        gui_shell_activate(dock->connection, window->id);
        return;
    }
    int pid = spawn(item->command[0], item->command);
    if (pid < 0) {
        printf("desktop: cannot start %s\n", item->command[0]);
        return;
    }
    /* The application lives its own life; we will not wait for it. */
    detach(pid);
    printf("desktop: started %s\n", item->app);
}

bool dock_owns(const struct gui_window *window)
{
    return window == dock;
}

void dock_handle_pointer(const struct gui_event *event)
{
    int item = item_at(event->pointer.x, event->pointer.y);
    if (event->pointer.kind == GUI_POINTER_DOWN) {
        pressed_item = item;
    } else if (event->pointer.kind == GUI_POINTER_UP) {
        /* Like a button: it counts if released where it was pressed. */
        if (item >= 0 && item == pressed_item) {
            activate(item);
        }
        pressed_item = -1;
    }
}

bool dock_create(struct gui_connection *connection)
{
    int shelf_width = 2 * SHELF_PADDING + ITEM_COUNT * ICON_SIZE + (ITEM_COUNT - 1) * ICON_GAP;
    /* Room on both sides for icons that grow under the pointer. */
    int width = shelf_width + 2 * ICON_SIZE;
    int x = (gui_screen_width(connection) - width) / 2;
    int y = gui_screen_height(connection) - WINDOW_HEIGHT;

    dock = gui_window_create(connection, GUI_WINDOW_PANEL, x, y, width, WINDOW_HEIGHT, "Dock",
                             GUI_CREATE_TRANSLUCENT | GUI_CREATE_BLUR_BEHIND | GUI_CREATE_NO_FOCUS);
    if (dock == NULL) {
        return false;
    }
    shelf = gfx_rect_make((width - shelf_width) / 2, WINDOW_HEIGHT - SHELF_HEIGHT - SHELF_BOTTOM_MARGIN, shelf_width,
                          SHELF_HEIGHT);
    /* Only the shelf reacts to the pointer and frosts what is behind it;
     * the rest of the window is empty and lets clicks through. */
    gui_window_set_input_region(dock, shelf);
    dock_update();
    printf("desktop: dock shelf at %d %d size %dx%d, %d items, first icon at %d %d\n", x + shelf.x, y + shelf.y,
           shelf.width, shelf.height, ITEM_COUNT, x + item_rect(0).x, y + item_rect(0).y);
    return true;
}
