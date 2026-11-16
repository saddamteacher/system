/*
 * The dock: a translucent shelf of application icons, centred at the
 * bottom of the screen. Clicking an icon starts the application, or brings
 * its window forward if it is already running.
 *
 * Two animations live here. Icons grow as the pointer comes near
 * ("magnification"), each driven by a spring so they follow the pointer
 * smoothly; and the icon of an application that is starting bounces until
 * its window appears.
 */
#include "desktop.h"

#include <gui/anim.h>
#include <math.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#define ICON_SIZE 48
#define ICON_GAP 14
#define SHELF_PADDING 12
#define SHELF_HEIGHT (ICON_SIZE + 2 * 10)
#define SHELF_RADIUS 20
#define SHELF_BOTTOM_MARGIN 8
/* The window is larger than the shelf: icons grow above and beside it. */
#define WINDOW_HEIGHT 132
#define SIDE_ROOM 56
#define INDICATOR_RADIUS 2

#define SHELF_TINT   GFX_RGBA(250, 250, 252, 105)
#define SHELF_BORDER GFX_RGBA(255, 255, 255, 90)
#define INDICATOR_COLOR GFX_RGBA(30, 30, 36, 200)

/* Magnification: the icon under the pointer grows by MAGNIFY_EXTRA; the
 * effect fades out over MAGNIFY_REACH pixels to either side. */
#define MAGNIFY_EXTRA 0.55f
#define MAGNIFY_REACH 110.0f
#define SPRING_STIFFNESS 420.0f
#define SPRING_DAMPING 34.0f /* a little under critical: a hint of overshoot */
/* While the pointer is over the dock, the area that reacts to it extends
 * this far above the shelf, so that it does not "leave" when it moves up
 * onto a grown icon. */
#define HOVER_EXTRA_HEIGHT 44

/* Launch bounce: hops of BOUNCE_HEIGHT pixels, one every BOUNCE_PERIOD_NS. */
#define BOUNCE_HEIGHT 20.0f
#define BOUNCE_PERIOD_NS 420000000ull
#define BOUNCE_TIMEOUT_NS 6000000000ull

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

struct item_state {
    struct anim_spring scale; /* 1.0 = normal size */
    bool bouncing;
    uint64_t bounce_start_ns;
    struct gfx_rect rect;     /* where it was last drawn, in the window */
};

static struct gui_window *dock;
static struct gfx_rect shelf; /* within the dock window */
static struct item_state states[ITEM_COUNT];
static int pressed_item = -1;
static bool pointer_inside;
static int pointer_x;
/* True while a frame event is awaited: the next step of the animation is
 * drawn when it arrives. */
static bool animating;
static bool magnified_logged;

/* ------------------------------------------------------------- layout */

/* Centre of an item when nothing is magnified. */
static int rest_center_x(int index)
{
    return shelf.x + SHELF_PADDING + index * (ICON_SIZE + ICON_GAP) + ICON_SIZE / 2;
}

static struct gfx_rect rest_rect(int index)
{
    return gfx_rect_make(rest_center_x(index) - ICON_SIZE / 2, shelf.y + (shelf.height - ICON_SIZE) / 2 - 2,
                         ICON_SIZE, ICON_SIZE);
}

/* How far above the ground a bouncing icon is at time `now_ns`. */
static int bounce_offset(const struct item_state *state, uint64_t now_ns)
{
    if (!state->bouncing) {
        return 0;
    }
    float phase = (float)((now_ns - state->bounce_start_ns) % BOUNCE_PERIOD_NS) / (float)BOUNCE_PERIOD_NS;
    /* Half a sine wave per hop: up and back down, landing softly. */
    return (int)(BOUNCE_HEIGHT * sinf(phase * (float)M_PI) + 0.5f);
}

/*
 * Places the icons for the current scales. Icons keep their gaps, so a
 * growing icon pushes its neighbours aside; the row stays centred on the
 * shelf and every icon stands on the same ground line and grows upwards.
 */
static void layout(uint64_t now_ns)
{
    int sizes[ITEM_COUNT];
    int total = (ITEM_COUNT - 1) * ICON_GAP;
    for (int i = 0; i < ITEM_COUNT; i++) {
        sizes[i] = (int)((float)ICON_SIZE * states[i].scale.value + 0.5f);
        total += sizes[i];
    }
    int x = shelf.x + (shelf.width - total) / 2;
    int ground = rest_rect(0).y + ICON_SIZE;
    for (int i = 0; i < ITEM_COUNT; i++) {
        states[i].rect = gfx_rect_make(x, ground - sizes[i] - bounce_offset(&states[i], now_ns), sizes[i], sizes[i]);
        x += sizes[i] + ICON_GAP;
    }
}

static int item_at(int x, int y)
{
    for (int i = 0; i < ITEM_COUNT; i++) {
        /* The gap between icons belongs to the nearer one, and everything
         * from the icon's top down to the shelf's bottom counts. */
        struct gfx_rect rect = states[i].rect;
        if (x >= rect.x - ICON_GAP / 2 && x < rect.x + rect.width + ICON_GAP / 2 && y >= rect.y
            && y < shelf.y + shelf.height) {
            return i;
        }
    }
    return -1;
}

/* ------------------------------------------------------------- windows */

/* An application's window, by the name its windows carry. */
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

/* Tells the window server where each window's icon is, so that minimizing
 * a window can fly it there. */
static void report_icon_rects(void)
{
    for (int w = 0; w < tracked_window_count; w++) {
        for (int i = 0; i < ITEM_COUNT; i++) {
            if (strcmp(tracked_windows[w].app, items[i].app) == 0) {
                struct gfx_rect rect = rest_rect(i);
                rect.x += dock->x;
                rect.y += dock->y;
                gui_shell_set_icon_rect(dock->connection, tracked_windows[w].id, rect);
            }
        }
    }
}

/* ------------------------------------------------------------- drawing */

static void draw(void)
{
    struct gfx_surface *surface = &dock->surface;
    gfx_clear(surface, gfx_surface_bounds(surface), GFX_TRANSPARENT);

    /* A bright rim under the tint reads as the edge of a glass pane. */
    gfx_fill_rounded_rect(surface, shelf, SHELF_RADIUS, SHELF_BORDER);
    gfx_fill_rounded_rect(surface, gfx_rect_inset(shelf, -1), SHELF_RADIUS - 1, SHELF_TINT);

    for (int i = 0; i < ITEM_COUNT; i++) {
        struct gfx_rect rect = states[i].rect;
        icon_draw(surface, items[i].icon, rect.x, rect.y, rect.width);
        if (window_of(items[i].app, false) != NULL) {
            /* A dot marks applications that have a window open. */
            gfx_fill_circle(surface, rect.x + rect.width / 2, shelf.y + shelf.height - 5, INDICATOR_RADIUS,
                            INDICATOR_COLOR);
        }
    }
}

/* The scale an icon should have for the current pointer position. */
static float target_scale(int index)
{
    if (!pointer_inside) {
        return 1.0f;
    }
    float distance = fabsf((float)(pointer_x - rest_center_x(index)));
    if (distance >= MAGNIFY_REACH) {
        return 1.0f;
    }
    /* A raised cosine: 1 at the pointer, 0 at the edge of its reach, and
     * smooth at both ends. */
    float falloff = cosf(distance / MAGNIFY_REACH * (float)M_PI / 2.0f);
    return 1.0f + MAGNIFY_EXTRA * falloff * falloff;
}

static void finish_bounce_if_due(int index, uint64_t now_ns)
{
    struct item_state *state = &states[index];
    if (!state->bouncing) {
        return;
    }
    uint64_t elapsed = now_ns - state->bounce_start_ns;
    bool window_arrived = window_of(items[index].app, false) != NULL;
    /* End on the ground, not in mid-air: only once the current hop is
     * over. A program that never shows a window stops after a while. */
    bool hop_complete = elapsed >= BOUNCE_PERIOD_NS && elapsed % BOUNCE_PERIOD_NS < BOUNCE_PERIOD_NS / 8;
    if ((window_arrived && hop_complete) || elapsed >= BOUNCE_TIMEOUT_NS) {
        state->bouncing = false;
        printf("desktop: bounce finished for %s after %llu ms\n", items[index].app,
               (unsigned long long)(elapsed / 1000000ull));
    }
}

/*
 * Advances all dock animations to the present and redraws. Returns true
 * if something is still moving, in which case another step is needed.
 */
static bool step(void)
{
    uint64_t now = clock_ns();
    bool moving = false;
    float largest = 1.0f;

    for (int i = 0; i < ITEM_COUNT; i++) {
        anim_spring_set_target(&states[i].scale, target_scale(i));
        float scale = anim_spring_update(&states[i].scale, now);
        largest = scale > largest ? scale : largest;
        finish_bounce_if_due(i, now);
        moving |= !anim_spring_at_rest(&states[i].scale) || states[i].bouncing;
    }
    layout(now);
    draw();

    if (!moving && pointer_inside && largest > 1.2f && !magnified_logged) {
        printf("desktop: dock magnified, largest icon %d percent\n", (int)(largest * 100.0f + 0.5f));
        magnified_logged = true;
    }
    return moving;
}

/* Draws one step and, while anything moves, asks to be called again when
 * that step is on screen. This paces the animation to the display. */
static void animate(void)
{
    animating = step();
    gui_window_commit(dock, gfx_surface_bounds(&dock->surface), animating);
}

void dock_update(void)
{
    report_icon_rects();
    if (!animating) {
        animate();
    }
}

void dock_handle_frame(void)
{
    if (animating) {
        animate();
    }
}

/* ---------------------------------------------------------------- input */

static void set_hover_region(bool hovering)
{
    struct gfx_rect region = shelf;
    if (hovering) {
        region = gfx_rect_make(shelf.x - SIDE_ROOM / 2, shelf.y - HOVER_EXTRA_HEIGHT, shelf.width + SIDE_ROOM,
                               shelf.height + HOVER_EXTRA_HEIGHT);
    }
    gui_window_set_input_region(dock, region);
}

static void start_bounce(int index)
{
    states[index].bouncing = true;
    states[index].bounce_start_ns = clock_ns();
    printf("desktop: bounce started for %s\n", items[index].app);
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
    if (states[index].bouncing) {
        return; /* already starting: a second click must not start it twice */
    }
    int pid = spawn(item->command[0], item->command);
    if (pid < 0) {
        printf("desktop: cannot start %s\n", item->command[0]);
        return;
    }
    /* The application lives its own life; we will not wait for it. */
    detach(pid);
    printf("desktop: started %s\n", item->app);
    start_bounce(index);
}

bool dock_owns(const struct gui_window *window)
{
    return window == dock;
}

void dock_handle_pointer(const struct gui_event *event)
{
    switch (event->pointer.kind) {
    case GUI_POINTER_LEAVE:
        pointer_inside = false;
        magnified_logged = false;
        pressed_item = -1;
        set_hover_region(false);
        break;
    case GUI_POINTER_DOWN:
        pressed_item = item_at(event->pointer.x, event->pointer.y);
        break;
    case GUI_POINTER_UP: {
        /* Like a button: it counts if released where it was pressed. */
        int item = item_at(event->pointer.x, event->pointer.y);
        if (item >= 0 && item == pressed_item) {
            activate(item);
        }
        pressed_item = -1;
        break;
    }
    default:
        break;
    }
    if (event->pointer.kind != GUI_POINTER_LEAVE) {
        if (!pointer_inside) {
            set_hover_region(true);
        }
        pointer_inside = true;
        pointer_x = event->pointer.x;
    }
    if (!animating) {
        animate();
    }
}

bool dock_create(struct gui_connection *connection)
{
    int shelf_width = 2 * SHELF_PADDING + ITEM_COUNT * ICON_SIZE + (ITEM_COUNT - 1) * ICON_GAP;
    int width = shelf_width + 2 * SIDE_ROOM;
    int x = (gui_screen_width(connection) - width) / 2;
    int y = gui_screen_height(connection) - WINDOW_HEIGHT;

    dock = gui_window_create(connection, GUI_WINDOW_PANEL, x, y, width, WINDOW_HEIGHT, "Dock",
                             GUI_CREATE_TRANSLUCENT | GUI_CREATE_BLUR_BEHIND | GUI_CREATE_NO_FOCUS);
    if (dock == NULL) {
        return false;
    }
    dock->x = x;
    dock->y = y;
    shelf = gfx_rect_make((width - shelf_width) / 2, WINDOW_HEIGHT - SHELF_HEIGHT - SHELF_BOTTOM_MARGIN, shelf_width,
                          SHELF_HEIGHT);
    for (int i = 0; i < ITEM_COUNT; i++) {
        anim_spring_init(&states[i].scale, 1.0f, SPRING_STIFFNESS, SPRING_DAMPING);
    }
    /* Only the shelf reacts to the pointer and frosts what is behind it;
     * the rest of the window is empty and lets clicks through. */
    set_hover_region(false);
    animate();
    printf("desktop: dock shelf at %d %d size %dx%d, %d items, first icon at %d %d\n", x + shelf.x, y + shelf.y,
           shelf.width, shelf.height, ITEM_COUNT, x + rest_rect(0).x, y + rest_rect(0).y);
    return true;
}
