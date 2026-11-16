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

bool icons_init(void)
{
    static void (*const painters[ICON_KIND_COUNT])(struct gfx_surface *) = {
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
