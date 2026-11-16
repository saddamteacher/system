/*
 * A demo window-system client: a window with animated content.
 *
 * usage: guidemo <title> <x> <y> <colour 0-2>
 *
 * It draws one frame, commits it asking for a frame event, and draws the
 * next when that arrives, so it animates at whatever rate the window
 * server composes. It reports its frame rate once per second.
 */
#include <gui/gui.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define INITIAL_WIDTH 360
#define INITIAL_HEIGHT 240
#define MARKER_SIZE 24
#define BAND_BOTTOM_MARGIN 34 /* from the band to the window's bottom edge */
#define BAND_HEIGHT 36
#define BLOCK_WIDTH 60

static const struct {
    gfx_color background, marker, accent;
} themes[] = {
    { GFX_RGB(236, 242, 255), GFX_RGB(0, 90, 255), GFX_RGB(0, 122, 255) },
    { GFX_RGB(255, 243, 230), GFX_RGB(255, 120, 0), GFX_RGB(255, 149, 0) },
    { GFX_RGB(232, 250, 236), GFX_RGB(0, 160, 60), GFX_RGB(52, 199, 89) },
};

static const char *title;
static struct gfx_font *font;
static gfx_color background, accent;

/* The layout follows the window's size, which the user can change. */
static int band_y(const struct gui_window *window)
{
    return window->height - BAND_BOTTOM_MARGIN - BAND_HEIGHT;
}

static void draw_static(struct gui_window *window, gfx_color marker)
{
    struct gfx_surface *surface = &window->surface;
    gfx_clear(surface, gfx_surface_bounds(surface), background);
    /* A patch of known colour for the automated check to look for. */
    gfx_clear(surface, gfx_rect_make(0, 0, MARKER_SIZE, MARKER_SIZE), marker);
    if (font != NULL) {
        gfx_draw_text(surface, font, 40, 28, title, -1, GFX_RGB(40, 40, 48));
        gfx_draw_text(surface, font, 40, 98, "Drag me by the title bar.", -1, GFX_RGB(110, 110, 120));
    }
}

/* Draws the parts that change every frame; returns the area it touched. */
static struct gfx_rect draw_frame(struct gui_window *window, unsigned frame, unsigned fps)
{
    struct gfx_surface *surface = &window->surface;
    struct gfx_rect band = gfx_rect_make(0, band_y(window), window->width, BAND_HEIGHT);
    struct gfx_rect status = gfx_rect_make(40, 50, 280, 28);

    gfx_clear(surface, band, background);
    /* A block bouncing between the edges, two pixels per frame. */
    int range = window->width - BLOCK_WIDTH;
    int position = (int)(frame * 2 % (unsigned)(2 * range));
    if (position > range) {
        position = 2 * range - position;
    }
    gfx_fill_rounded_rect(surface, gfx_rect_make(position, band.y, BLOCK_WIDTH, BAND_HEIGHT), 8, accent);

    gfx_clear(surface, status, background);
    if (font != NULL) {
        char text[64];
        snprintf(text, sizeof(text), "frame %u, %u fps", frame, fps);
        gfx_draw_text(surface, font, status.x, status.y + 19, text, -1, GFX_RGB(40, 40, 48));
    }
    return gfx_rect_union(band, status);
}

int main(int argc, char **argv)
{
    if (argc < 5) {
        printf("usage: guidemo <title> <x> <y> <colour>\n");
        return 2;
    }
    title = argv[1];
    int theme = atoi(argv[4]) % 3;
    background = themes[theme].background;
    accent = themes[theme].accent;

    /* The title doubles as the application name. */
    struct gui_connection *connection = gui_connect(title);
    if (connection == NULL) {
        printf("guidemo %s: no window server\n", title);
        return 1;
    }
    font = gfx_font_load("/fonts/Inter-Regular-13.sdf");
    struct gui_window *window =
        gui_window_create(connection, GUI_WINDOW_NORMAL, atoi(argv[2]), atoi(argv[3]), INITIAL_WIDTH, INITIAL_HEIGHT,
                          title, 0);
    if (window == NULL) {
        printf("guidemo %s: cannot create a window\n", title);
        return 1;
    }

    unsigned frame = 0, fps = 0, frames_this_second = 0;
    uint64_t second_started = uptime_ms();

    draw_static(window, themes[theme].marker);
    draw_frame(window, frame, fps);
    gui_window_commit(window, gfx_surface_bounds(&window->surface), true);
    printf("guidemo %s: marker %06x background %06x\n", title, (unsigned)(themes[theme].marker & 0xFFFFFF),
           (unsigned)(background & 0xFFFFFF));

    for (;;) {
        struct gui_event event;
        if (gui_next_event(connection, &event, -1) < 0) {
            break; /* the window server is gone */
        }
        if (event.type == GUI_MSG_CLOSE_REQUEST
            || (event.type == GUI_MSG_KEY && event.key.pressed && event.key.character == 'q')) {
            break;
        }
        if (event.type == GUI_MSG_RESIZE_REQUEST) {
            /* A new, empty surface of the requested size: redraw it all. */
            if (gui_window_resize(window, event.resize.width, event.resize.height)) {
                draw_static(window, themes[theme].marker);
                draw_frame(window, frame, fps);
                gui_window_commit(window, gfx_surface_bounds(&window->surface), true);
            }
            continue;
        }
        if (event.type != GUI_MSG_FRAME) {
            continue;
        }
        if (frame == 0) {
            printf("guidemo %s: shown\n", title);
        }
        frame++;
        frames_this_second++;
        uint64_t now = uptime_ms();
        if (now - second_started >= 1000) {
            fps = (unsigned)(frames_this_second * 1000 / (now - second_started));
            printf("guidemo %s: %u fps\n", title, fps);
            frames_this_second = 0;
            second_started = now;
        }
        gui_window_commit(window, draw_frame(window, frame, fps), true);
    }

    gui_disconnect(connection);
    return 0;
}
