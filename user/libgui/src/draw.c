#include <gui/gfx.h>

#include <math.h>

#include "pixel.h"

static uint32_t *row_at(const struct gfx_surface *surface, int y)
{
    return surface->pixels + (size_t)y * (size_t)surface->stride;
}

void gfx_clear(struct gfx_surface *surface, struct gfx_rect rect, gfx_color color)
{
    rect = gfx_rect_intersect(rect, surface->clip);
    uint32_t pixel = premultiply(color, 255);

    for (int y = rect.y; y < rect.y + rect.height; y++) {
        uint32_t *row = row_at(surface, y) + rect.x;
        for (int x = 0; x < rect.width; x++) {
            row[x] = pixel;
        }
    }
}

/* Blends one premultiplied pixel value over a horizontal run. */
static void blend_span(uint32_t *row, int count, uint32_t pixel)
{
    uint32_t alpha = pixel >> 24;
    if (alpha == 255) {
        for (int x = 0; x < count; x++) {
            row[x] = pixel;
        }
    } else if (alpha != 0) {
        uint32_t keep = 255 - alpha;
        for (int x = 0; x < count; x++) {
            row[x] = pixel + scale_pixel(row[x], keep);
        }
    }
}

void gfx_fill_rect(struct gfx_surface *surface, struct gfx_rect rect, gfx_color color)
{
    rect = gfx_rect_intersect(rect, surface->clip);
    uint32_t pixel = premultiply(color, 255);

    for (int y = rect.y; y < rect.y + rect.height; y++) {
        blend_span(row_at(surface, y) + rect.x, rect.width, pixel);
    }
}

void gfx_stroke_rect(struct gfx_surface *surface, struct gfx_rect rect, gfx_color color)
{
    if (rect.width < 1 || rect.height < 1) {
        return;
    }
    gfx_fill_rect(surface, gfx_rect_make(rect.x, rect.y, rect.width, 1), color);
    gfx_fill_rect(surface, gfx_rect_make(rect.x, rect.y + rect.height - 1, rect.width, 1), color);
    gfx_fill_rect(surface, gfx_rect_make(rect.x, rect.y + 1, 1, rect.height - 2), color);
    gfx_fill_rect(surface, gfx_rect_make(rect.x + rect.width - 1, rect.y + 1, 1, rect.height - 2), color);
}

void gfx_fill_gradient(struct gfx_surface *surface, struct gfx_rect rect, gfx_color top, gfx_color bottom)
{
    struct gfx_rect visible = gfx_rect_intersect(rect, surface->clip);
    int steps = rect.height > 1 ? rect.height - 1 : 1;

    for (int y = visible.y; y < visible.y + visible.height; y++) {
        /* Interpolate each channel by how far down the row is. */
        uint32_t t = (uint32_t)(y - rect.y) * 255u / (uint32_t)steps;
        uint32_t s = 255 - t;
        gfx_color color = GFX_RGBA(div255(GFX_RED(top) * s + GFX_RED(bottom) * t),
                                   div255(GFX_GREEN(top) * s + GFX_GREEN(bottom) * t),
                                   div255(GFX_BLUE(top) * s + GFX_BLUE(bottom) * t),
                                   div255(GFX_ALPHA(top) * s + GFX_ALPHA(bottom) * t));
        blend_span(row_at(surface, y) + visible.x, visible.width, premultiply(color, 255));
    }
}

/*
 * How much of the pixel at (px, py) lies inside a circle of `radius` around
 * (cx, cy), from 0 to 255. Pixels are treated as points at their centres
 * and the edge as a one-pixel-wide ramp: coverage falls from 1 to 0 as the
 * distance grows from radius - 0.5 to radius + 0.5. That ramp is what makes
 * the curve look smooth instead of stepped.
 */
static uint32_t circle_coverage(float px, float py, float cx, float cy, float radius)
{
    float dx = px - cx;
    float dy = py - cy;
    float inside = radius + 0.5f - sqrtf(dx * dx + dy * dy);

    if (inside <= 0.0f) {
        return 0;
    }
    if (inside >= 1.0f) {
        return 255;
    }
    return (uint32_t)(inside * 255.0f + 0.5f);
}

static void blend_pixel(struct gfx_surface *surface, int x, int y, gfx_color color, uint32_t coverage)
{
    if (coverage != 0 && gfx_rect_contains(surface->clip, x, y)) {
        uint32_t *pixel = row_at(surface, y) + x;
        *pixel = blend_over(*pixel, premultiply(color, coverage));
    }
}

void gfx_fill_rounded_rect(struct gfx_surface *surface, struct gfx_rect rect, int radius, gfx_color color)
{
    if (gfx_rect_is_empty(rect)) {
        return;
    }
    int limit = (rect.width < rect.height ? rect.width : rect.height) / 2;
    if (radius > limit) {
        radius = limit;
    }
    if (radius <= 0) {
        gfx_fill_rect(surface, rect, color);
        return;
    }

    /* The shape is a cross of two plain rectangles plus four corners. */
    gfx_fill_rect(surface, gfx_rect_make(rect.x, rect.y + radius, rect.width, rect.height - 2 * radius), color);
    gfx_fill_rect(surface, gfx_rect_make(rect.x + radius, rect.y, rect.width - 2 * radius, radius), color);
    gfx_fill_rect(surface,
                  gfx_rect_make(rect.x + radius, rect.y + rect.height - radius, rect.width - 2 * radius, radius),
                  color);

    /* The top-left corner is computed in its own coordinates: a square of
     * `radius` pixels with the circle centred on its bottom-right corner.
     * The other three corners are its mirror images. */
    for (int j = 0; j < radius; j++) {
        for (int i = 0; i < radius; i++) {
            uint32_t coverage =
                circle_coverage((float)i + 0.5f, (float)j + 0.5f, (float)radius, (float)radius, (float)radius);
            blend_pixel(surface, rect.x + i, rect.y + j, color, coverage);
            blend_pixel(surface, rect.x + rect.width - 1 - i, rect.y + j, color, coverage);
            blend_pixel(surface, rect.x + i, rect.y + rect.height - 1 - j, color, coverage);
            blend_pixel(surface, rect.x + rect.width - 1 - i, rect.y + rect.height - 1 - j, color, coverage);
        }
    }
}

void gfx_fill_circle(struct gfx_surface *surface, int center_x, int center_y, int radius, gfx_color color)
{
    if (radius <= 0) {
        return;
    }
    gfx_fill_rounded_rect(surface, gfx_rect_make(center_x - radius, center_y - radius, 2 * radius, 2 * radius),
                          radius, color);
}

/*
 * Works out which part of a source rectangle ends up visible when drawn at
 * (x, y): clipped to the source's bounds and the destination's clip. On
 * return *x, *y and *source_rect describe that part. False if nothing is.
 */
static bool clip_transfer(const struct gfx_surface *surface, int *x, int *y, const struct gfx_surface *source,
                          struct gfx_rect *source_rect)
{
    struct gfx_rect bounded = gfx_rect_intersect(*source_rect, gfx_surface_bounds(source));
    /* Where the bounded source lands in the destination. */
    struct gfx_rect target = gfx_rect_make(*x + (bounded.x - source_rect->x), *y + (bounded.y - source_rect->y),
                                           bounded.width, bounded.height);
    struct gfx_rect visible = gfx_rect_intersect(target, surface->clip);
    if (gfx_rect_is_empty(visible)) {
        return false;
    }
    source_rect->x = bounded.x + (visible.x - target.x);
    source_rect->y = bounded.y + (visible.y - target.y);
    source_rect->width = visible.width;
    source_rect->height = visible.height;
    *x = visible.x;
    *y = visible.y;
    return true;
}

void gfx_blit(struct gfx_surface *surface, int x, int y, const struct gfx_surface *source,
              struct gfx_rect source_rect)
{
    if (!clip_transfer(surface, &x, &y, source, &source_rect)) {
        return;
    }
    for (int row = 0; row < source_rect.height; row++) {
        const uint32_t *from = row_at(source, source_rect.y + row) + source_rect.x;
        uint32_t *to = row_at(surface, y + row) + x;
        for (int column = 0; column < source_rect.width; column++) {
            to[column] = from[column];
        }
    }
}

void gfx_blend(struct gfx_surface *surface, int x, int y, const struct gfx_surface *source,
               struct gfx_rect source_rect, unsigned opacity)
{
    if (opacity == 0 || !clip_transfer(surface, &x, &y, source, &source_rect)) {
        return;
    }
    for (int row = 0; row < source_rect.height; row++) {
        const uint32_t *from = row_at(source, source_rect.y + row) + source_rect.x;
        uint32_t *to = row_at(surface, y + row) + x;
        if (opacity >= 255) {
            for (int column = 0; column < source_rect.width; column++) {
                to[column] = blend_over(to[column], from[column]);
            }
        } else {
            for (int column = 0; column < source_rect.width; column++) {
                to[column] = blend_over(to[column], scale_pixel(from[column], opacity));
            }
        }
    }
}
