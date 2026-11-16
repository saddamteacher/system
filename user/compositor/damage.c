/*
 * Damage tracking: the list of screen rectangles that changed since the
 * last frame. Redrawing only these, instead of the whole screen, is what
 * makes a software compositor fast enough: moving the mouse touches a few
 * hundred pixels, not a million.
 */
#include "compositor.h"

static struct gfx_rect rects[DAMAGE_MAX_RECTS];
static int rect_count;

static bool touches(struct gfx_rect a, struct gfx_rect b)
{
    return !gfx_rect_is_empty(gfx_rect_intersect(a, b));
}

void damage_add(struct gfx_rect rect)
{
    rect = gfx_rect_intersect(rect, gfx_surface_bounds(&screen));
    if (gfx_rect_is_empty(rect)) {
        return;
    }

    /* Overlapping rectangles would make the overlap be drawn twice, so
     * merge them. A merge can make the result touch further rectangles,
     * hence the restart. */
    for (int i = 0; i < rect_count;) {
        if (touches(rects[i], rect)) {
            rect = gfx_rect_union(rect, rects[i]);
            rects[i] = rects[--rect_count];
            i = 0;
        } else {
            i++;
        }
    }
    if (rect_count == DAMAGE_MAX_RECTS) {
        /* Too fragmented to track precisely: fall back to one rectangle
         * around everything. Correct, just more drawing. */
        for (int i = 0; i < rect_count; i++) {
            rect = gfx_rect_union(rect, rects[i]);
        }
        rect_count = 0;
    }
    rects[rect_count++] = rect;
}

bool damage_pending(void)
{
    return rect_count > 0;
}

int damage_take(struct gfx_rect *out, int capacity)
{
    int count = rect_count < capacity ? rect_count : capacity;
    for (int i = 0; i < count; i++) {
        out[i] = rects[i];
    }
    rect_count = 0;
    return count;
}
