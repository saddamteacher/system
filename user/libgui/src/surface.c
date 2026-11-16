#include <gui/gfx.h>

#include <stdlib.h>

struct gfx_rect gfx_rect_intersect(struct gfx_rect a, struct gfx_rect b)
{
    int left = a.x > b.x ? a.x : b.x;
    int top = a.y > b.y ? a.y : b.y;
    int right = a.x + a.width < b.x + b.width ? a.x + a.width : b.x + b.width;
    int bottom = a.y + a.height < b.y + b.height ? a.y + a.height : b.y + b.height;

    if (right <= left || bottom <= top) {
        return gfx_rect_make(0, 0, 0, 0);
    }
    return gfx_rect_make(left, top, right - left, bottom - top);
}

struct gfx_rect gfx_rect_union(struct gfx_rect a, struct gfx_rect b)
{
    if (gfx_rect_is_empty(a)) {
        return b;
    }
    if (gfx_rect_is_empty(b)) {
        return a;
    }
    int left = a.x < b.x ? a.x : b.x;
    int top = a.y < b.y ? a.y : b.y;
    int right = a.x + a.width > b.x + b.width ? a.x + a.width : b.x + b.width;
    int bottom = a.y + a.height > b.y + b.height ? a.y + a.height : b.y + b.height;
    return gfx_rect_make(left, top, right - left, bottom - top);
}

struct gfx_rect gfx_rect_inset(struct gfx_rect rect, int amount)
{
    return gfx_rect_make(rect.x - amount, rect.y - amount, rect.width + 2 * amount, rect.height + 2 * amount);
}

void gfx_surface_init(struct gfx_surface *surface, uint32_t *pixels, int width, int height, int stride)
{
    surface->pixels = pixels;
    surface->width = width;
    surface->height = height;
    surface->stride = stride;
    surface->clip = gfx_rect_make(0, 0, width, height);
    surface->owns_pixels = false;
}

bool gfx_surface_create(struct gfx_surface *surface, int width, int height)
{
    if (width <= 0 || height <= 0) {
        return false;
    }
    uint32_t *pixels = calloc((size_t)width * (size_t)height, sizeof(uint32_t));
    if (pixels == NULL) {
        return false;
    }
    gfx_surface_init(surface, pixels, width, height, width);
    surface->owns_pixels = true;
    return true;
}

void gfx_surface_destroy(struct gfx_surface *surface)
{
    if (surface->owns_pixels) {
        free(surface->pixels);
    }
    surface->pixels = NULL;
    surface->owns_pixels = false;
}

void gfx_set_clip(struct gfx_surface *surface, struct gfx_rect clip)
{
    surface->clip = gfx_rect_intersect(clip, gfx_surface_bounds(surface));
}

void gfx_reset_clip(struct gfx_surface *surface)
{
    surface->clip = gfx_surface_bounds(surface);
}
