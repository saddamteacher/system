/*
 * Application icons, drawn by code: rounded tiles with a simple white
 * symbol. Each is drawn once at a large size; the dock scales it down.
 */
#include "desktop.h"

static struct gfx_surface masters[ICON_KIND_COUNT];

static void draw_tile(struct gfx_surface *surface, gfx_color top, gfx_color bottom)
{
    int size = surface->width;
    int radius = size * 22 / 100;
    /* A gradient cannot be drawn rounded directly: draw it square, then
     * keep only what lies inside a rounded mask. */
    struct gfx_surface mask;
    if (!gfx_surface_create(&mask, size, size)) {
        return;
    }
    gfx_fill_rounded_rect(&mask, gfx_rect_make(2, 2, size - 4, size - 4), radius, GFX_WHITE);
    gfx_fill_gradient(surface, gfx_surface_bounds(surface), top, bottom);
    for (int i = 0; i < size * size; i++) {
        uint32_t coverage = mask.pixels[i] >> 24;
        uint32_t pixel = surface->pixels[i];
        /* Scale all four channels: the result stays premultiplied. */
        uint32_t red_blue = (((pixel & 0x00FF00FF) * coverage + 0x00800080) >> 8) & 0x00FF00FF;
        uint32_t alpha_green = ((((pixel >> 8) & 0x00FF00FF) * coverage + 0x00800080)) & 0xFF00FF00;
        surface->pixels[i] = red_blue | alpha_green;
    }
    gfx_surface_destroy(&mask);
    /* A soft highlight along the top edge gives the tile some depth. */
    gfx_fill_rounded_rect(surface, gfx_rect_make(8, 5, size - 16, 3), 1, GFX_RGBA(255, 255, 255, 70));
}

static void draw_blue(struct gfx_surface *surface)
{
    int size = surface->width;
    draw_tile(surface, GFX_RGB(70, 160, 255), GFX_RGB(0, 90, 230));
    /* Three bars of growing height. */
    for (int i = 0; i < 3; i++) {
        int height = size * (22 + 14 * i) / 100;
        gfx_fill_rounded_rect(surface, gfx_rect_make(size * (26 + 18 * i) / 100, size * 72 / 100 - height,
                                                     size * 13 / 100, height), 4, GFX_WHITE);
    }
}

static void draw_orange(struct gfx_surface *surface)
{
    int size = surface->width;
    draw_tile(surface, GFX_RGB(255, 180, 70), GFX_RGB(255, 110, 20));
    /* A ring. */
    gfx_fill_circle(surface, size / 2, size / 2, size * 26 / 100, GFX_WHITE);
    gfx_fill_circle(surface, size / 2, size / 2, size * 15 / 100, GFX_RGB(255, 140, 40));
}

static void draw_green(struct gfx_surface *surface)
{
    int size = surface->width;
    draw_tile(surface, GFX_RGB(110, 220, 130), GFX_RGB(30, 170, 80));
    /* Four dots in a square. */
    for (int i = 0; i < 4; i++) {
        gfx_fill_circle(surface, size * (36 + 28 * (i % 2)) / 100, size * (36 + 28 * (i / 2)) / 100,
                        size * 9 / 100, GFX_WHITE);
    }
}

/* A straight stroke with round ends, drawn as a row of overlapping dots. */
static void draw_stroke(struct gfx_surface *surface, int x0, int y0, int x1, int y1, int thickness, gfx_color color)
{
    int dx = x1 - x0, dy = y1 - y0;
    int steps = (dx < 0 ? -dx : dx) > (dy < 0 ? -dy : dy) ? (dx < 0 ? -dx : dx) : (dy < 0 ? -dy : dy);
    for (int i = 0; i <= steps; i++) {
        int x = x0 + (steps > 0 ? dx * i / steps : 0);
        int y = y0 + (steps > 0 ? dy * i / steps : 0);
        gfx_fill_circle(surface, x, y, thickness / 2, color);
    }
}

/* A dark screen with a prompt: a chevron and a cursor bar. */
static void draw_terminal(struct gfx_surface *surface)
{
    int size = surface->width;
    draw_tile(surface, GFX_RGB(70, 74, 88), GFX_RGB(26, 28, 36));
    draw_stroke(surface, size * 28 / 100, size * 34 / 100, size * 46 / 100, size * 50 / 100, 8, GFX_WHITE);
    draw_stroke(surface, size * 46 / 100, size * 50 / 100, size * 28 / 100, size * 66 / 100, 8, GFX_WHITE);
    gfx_fill_rounded_rect(surface, gfx_rect_make(size * 52 / 100, size * 62 / 100, size * 22 / 100, 8), 4,
                          GFX_RGB(120, 220, 140));
}

/* A folder: a tab behind a body. */
static void draw_files(struct gfx_surface *surface)
{
    int size = surface->width;
    draw_tile(surface, GFX_RGB(120, 190, 255), GFX_RGB(40, 120, 235));
    gfx_fill_rounded_rect(surface, gfx_rect_make(size * 22 / 100, size * 28 / 100, size * 28 / 100, size * 16 / 100), 6,
                          GFX_RGBA(255, 255, 255, 190));
    gfx_fill_rounded_rect(surface, gfx_rect_make(size * 22 / 100, size * 36 / 100, size * 56 / 100, size * 38 / 100), 7,
                          GFX_WHITE);
}

/* A gear: a disc with teeth around it and a hole in the middle. */
static void draw_settings(struct gfx_surface *surface)
{
    /* Unit vectors for eight directions, scaled by 1000. */
    static const int directions[8][2] = { { 1000, 0 },  { 707, 707 },   { 0, 1000 },  { -707, 707 },
                                          { -1000, 0 }, { -707, -707 }, { 0, -1000 }, { 707, -707 } };
    int size = surface->width;
    int center = size / 2;
    draw_tile(surface, GFX_RGB(176, 180, 190), GFX_RGB(108, 112, 124));
    for (int i = 0; i < 8; i++) {
        int reach = size * 27 / 100;
        gfx_fill_circle(surface, center + directions[i][0] * reach / 1000, center + directions[i][1] * reach / 1000,
                        size * 7 / 100, GFX_WHITE);
    }
    gfx_fill_circle(surface, center, center, size * 24 / 100, GFX_WHITE);
    gfx_fill_circle(surface, center, center, size * 10 / 100, GFX_RGB(130, 134, 146));
}

/* The letter "i" for information: a dot over a bar. */
static void draw_about(struct gfx_surface *surface)
{
    int size = surface->width;
    draw_tile(surface, GFX_RGB(150, 130, 250), GFX_RGB(94, 76, 220));
    gfx_fill_circle(surface, size / 2, size * 30 / 100, size * 7 / 100, GFX_WHITE);
    gfx_fill_rounded_rect(surface, gfx_rect_make(size * 44 / 100, size * 42 / 100, size * 12 / 100, size * 34 / 100), 5,
                          GFX_WHITE);
}

bool icons_init(void)
{
    static void (*const painters[ICON_KIND_COUNT])(struct gfx_surface *) = {
        [ICON_TERMINAL] = draw_terminal,
        [ICON_FILES] = draw_files,
        [ICON_SETTINGS] = draw_settings,
        [ICON_ABOUT] = draw_about,
        [ICON_DEMO_BLUE] = draw_blue,
        [ICON_DEMO_ORANGE] = draw_orange,
        [ICON_DEMO_GREEN] = draw_green,
    };
    for (int kind = 0; kind < ICON_KIND_COUNT; kind++) {
        if (!gfx_surface_create(&masters[kind], ICON_MASTER_SIZE, ICON_MASTER_SIZE)) {
            return false;
        }
        painters[kind](&masters[kind]);
    }
    return true;
}

void icon_draw(struct gfx_surface *surface, enum icon_kind kind, int x, int y, int size)
{
    gfx_blend_scaled(surface, gfx_rect_make(x, y, size, size), &masters[kind], gfx_surface_bounds(&masters[kind]),
                     255);
}
