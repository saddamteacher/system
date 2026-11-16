/*
 * The wallpaper: an original picture computed when the desktop starts. A
 * dusk-coloured gradient with a few large, soft glows.
 */
#include "desktop.h"

#include <math.h>

struct glow {
    float x, y;      /* centre, as a fraction of the screen */
    float radius;    /* as a fraction of the screen width */
    float red, green, blue;
    float strength;
};

static const struct glow glows[] = {
    { 0.18f, 0.25f, 0.55f, 0.20f, 0.35f, 0.95f, 0.55f },
    { 0.85f, 0.15f, 0.45f, 0.95f, 0.45f, 0.65f, 0.40f },
    { 0.70f, 0.95f, 0.60f, 1.00f, 0.62f, 0.25f, 0.55f },
    { 0.05f, 0.90f, 0.40f, 0.35f, 0.15f, 0.55f, 0.45f },
};

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

static gfx_color wallpaper_pixel(int x, int y, int width, int height)
{
    float u = (float)x / (float)width;
    float v = (float)y / (float)height;

    /* Base: deep blue at the top to dusky violet at the bottom, tilted a
     * little so it is not a plain vertical ramp. */
    float t = clamp01(v * 0.85f + u * 0.15f);
    float red = 0.09f + 0.30f * t;
    float green = 0.11f + 0.10f * t;
    float blue = 0.26f + 0.16f * t;

    float aspect = (float)height / (float)width;
    for (unsigned i = 0; i < sizeof(glows) / sizeof(glows[0]); i++) {
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

bool wallpaper_create(struct gui_connection *connection)
{
    int width = gui_screen_width(connection), height = gui_screen_height(connection);
    struct gui_window *window = gui_window_create(connection, GUI_WINDOW_DESKTOP, 0, 0, width, height, "Wallpaper", 0);
    if (window == NULL) {
        return false;
    }
    for (int y = 0; y < height; y++) {
        uint32_t *row = window->surface.pixels + (size_t)y * (size_t)window->surface.stride;
        for (int x = 0; x < width; x++) {
            row[x] = wallpaper_pixel(x, y, width, height); /* opaque, so already premultiplied */
        }
    }
    gui_window_commit(window, gfx_surface_bounds(&window->surface), false);
    return true;
}
