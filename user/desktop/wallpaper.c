/*
 * The wallpaper: an original picture computed by the desktop, in one of a
 * few styles. Each is a gradient with several large, soft glows.
 */
#include "desktop.h"

#include <math.h>

struct glow {
    float x, y;      /* centre, as a fraction of the screen */
    float radius;    /* as a fraction of the screen width */
    float red, green, blue;
    float strength;
};

#define GLOW_COUNT 4

/* A wallpaper style: the gradient's colour at the top and how it changes
 * towards the bottom, and the glows laid over it. */
struct style {
    float top[3], slope[3];
    struct glow glows[GLOW_COUNT];
};

static const struct style styles[SHELL_WALLPAPER_COUNT] = {
    [SHELL_WALLPAPER_DUSK] = {
        { 0.09f, 0.11f, 0.26f }, { 0.30f, 0.10f, 0.16f },
        { { 0.18f, 0.25f, 0.55f, 0.20f, 0.35f, 0.95f, 0.55f },
          { 0.85f, 0.15f, 0.45f, 0.95f, 0.45f, 0.65f, 0.40f },
          { 0.70f, 0.95f, 0.60f, 1.00f, 0.62f, 0.25f, 0.55f },
          { 0.05f, 0.90f, 0.40f, 0.35f, 0.15f, 0.55f, 0.45f } },
    },
    [SHELL_WALLPAPER_OCEAN] = {
        { 0.02f, 0.10f, 0.22f }, { 0.02f, 0.26f, 0.30f },
        { { 0.20f, 0.20f, 0.55f, 0.10f, 0.45f, 0.85f, 0.55f },
          { 0.80f, 0.30f, 0.50f, 0.20f, 0.80f, 0.85f, 0.45f },
          { 0.60f, 0.95f, 0.60f, 0.55f, 0.95f, 0.85f, 0.45f },
          { 0.05f, 0.85f, 0.40f, 0.05f, 0.20f, 0.50f, 0.50f } },
    },
    [SHELL_WALLPAPER_FOREST] = {
        { 0.05f, 0.14f, 0.12f }, { 0.12f, 0.20f, 0.02f },
        { { 0.20f, 0.25f, 0.55f, 0.10f, 0.50f, 0.35f, 0.50f },
          { 0.85f, 0.20f, 0.45f, 0.85f, 0.85f, 0.40f, 0.35f },
          { 0.65f, 0.95f, 0.60f, 0.30f, 0.70f, 0.25f, 0.50f },
          { 0.05f, 0.90f, 0.40f, 0.02f, 0.25f, 0.22f, 0.50f } },
    },
};

static struct gui_window *wallpaper;

static float clamp01(float value)
{
    return value < 0.0f ? 0.0f : (value > 1.0f ? 1.0f : value);
}

/* A cheap, repeatable pseudo-random value in [0, 1) for a pixel. */
static float pixel_noise(int x, int y)
{
    uint32_t hash = (uint32_t)x * 374761393u + (uint32_t)y * 668265263u;
    hash = (hash ^ (hash >> 13)) * 1274126177u;
    return (float)((hash ^ (hash >> 16)) & 0xFFFF) / 65536.0f;
}

static gfx_color wallpaper_pixel(const struct style *style, int x, int y, int width, int height)
{
    const struct glow *glows = style->glows;
    float u = (float)x / (float)width;
    float v = (float)y / (float)height;

    /* Base: one colour at the top shading into another at the bottom,
     * tilted a little so it is not a plain vertical ramp. */
    float t = clamp01(v * 0.85f + u * 0.15f);
    float red = style->top[0] + style->slope[0] * t;
    float green = style->top[1] + style->slope[1] * t;
    float blue = style->top[2] + style->slope[2] * t;

    float aspect = (float)height / (float)width;
    for (unsigned i = 0; i < GLOW_COUNT; i++) {
        float dx = u - glows[i].x;
        float dy = (v - glows[i].y) * aspect;
        float distance = sqrtf(dx * dx + dy * dy) / glows[i].radius;
        if (distance < 1.0f) {
            /* Smooth fall-off: full strength at the centre, zero slope at
             * both ends, so no edge of the glow is visible. */
            float falloff = 1.0f - distance;
            falloff = falloff * falloff * (3.0f - 2.0f * falloff);
            float amount = falloff * glows[i].strength;
            red += (glows[i].red - red) * amount;
            green += (glows[i].green - green) * amount;
            blue += (glows[i].blue - blue) * amount;
        }
    }

    /* 8 bits per channel cannot represent so gentle a gradient: it would
     * show bands. Adding less than one step of noise before rounding
     * ("dithering") hides them. */
    float noise = (pixel_noise(x, y) - 0.5f) / 255.0f;
    return GFX_RGB((unsigned)(clamp01(red + noise) * 255.0f + 0.5f), (unsigned)(clamp01(green + noise) * 255.0f + 0.5f),
                   (unsigned)(clamp01(blue + noise) * 255.0f + 0.5f));
}

void wallpaper_apply(void)
{
    const struct style *style = &styles[shell_settings.wallpaper % SHELL_WALLPAPER_COUNT];
    int width = wallpaper->width, height = wallpaper->height;

    for (int y = 0; y < height; y++) {
        uint32_t *row = wallpaper->surface.pixels + (size_t)y * (size_t)wallpaper->surface.stride;
        for (int x = 0; x < width; x++) {
            row[x] = wallpaper_pixel(style, x, y, width, height); /* opaque, so already premultiplied */
        }
    }
    gui_window_commit(wallpaper, gfx_surface_bounds(&wallpaper->surface), false);
}

bool wallpaper_create(struct gui_connection *connection)
{
    wallpaper = gui_window_create(connection, GUI_WINDOW_DESKTOP, 0, 0, gui_screen_width(connection),
                                  gui_screen_height(connection), "Wallpaper", 0);
    if (wallpaper == NULL) {
        return false;
    }
    wallpaper_apply();
    return true;
}
