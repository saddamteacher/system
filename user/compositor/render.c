/*
 * Composition: building the picture on screen out of the windows.
 *
 * Frames are composed in a back buffer and then copied to the display, so
 * the display never shows a partly drawn state. Only damaged rectangles
 * are composed and copied.
 */
#include "compositor.h"

#include <display.h>
#include <gui/pixel.h>
#include <stdio.h>

struct gfx_surface screen;
struct gfx_surface back_buffer;

/* Shown where no window covers the screen (normally the desktop's
 * wallpaper window covers all of it). */
#define BACKGROUND_COLOR GFX_RGB(58, 66, 98)

bool render_init(void)
{
    struct sd_fb_info info;
    if (display_info(&info) != 0) {
        printf("compositor: no display\n");
        return false;
    }
    uint32_t *memory = display_map();
    if (memory == NULL) {
        printf("compositor: the display is in use by another program\n");
        return false;
    }
    gfx_surface_init(&screen, memory, (int)info.width, (int)info.height, (int)(info.pitch / 4));
    if (!gfx_surface_create(&back_buffer, screen.width, screen.height)) {
        printf("compositor: out of memory for the back buffer\n");
        return false;
    }
    return true;
}

/* The part of a blur-behind window that is actually frosted: its input
 * region if it has one (the visible panel), else all of it. */
static struct gfx_rect blur_rect(const struct window *window)
{
    struct gfx_rect rect = window_content_rect(window);
    if (window->has_input_region) {
        rect = gfx_rect_make(window->x + window->input_region.x, window->y + window->input_region.y,
                             window->input_region.width, window->input_region.height);
    }
    return gfx_rect_intersect(rect, gfx_surface_bounds(&back_buffer));
}

void render_scene_changed(struct gfx_rect rect)
{
    for (int i = 0; i < window_stack_count; i++) {
        struct window *window = window_stack[i];
        if ((window->flags & GUI_CREATE_BLUR_BEHIND) && window->backdrop_valid
            && !gfx_rect_is_empty(gfx_rect_intersect(blur_rect(window), rect))) {
            window->backdrop_valid = false;
        }
    }
}

/* Weighted average of two pixels: `weight` of 255 gives all of `b`. */
static uint32_t mix(uint32_t a, uint32_t b, uint32_t weight)
{
    return scale_pixel(a, 255 - weight) + scale_pixel(b, weight);
}

/* Blurs what is composed behind a panel and keeps the result. */
static bool refresh_backdrop(struct window *window, struct gfx_rect area)
{
    if (window->backdrop.pixels == NULL || window->backdrop.width < area.width
        || window->backdrop.height < area.height) {
        gfx_surface_destroy(&window->backdrop);
        if (!gfx_surface_create(&window->backdrop, area.width, area.height)) {
            return false;
        }
    }
    if (!gfx_blur_backdrop(&window->backdrop, &back_buffer, area)) {
        return false;
    }
    window->backdrop_valid = true;
    return true;
}

/*
 * Draws a translucent panel over a blurred copy of what is behind it, the
 * "frosted glass" look. `area` is the panel's frosted rectangle.
 *
 * Blurring is by far the most expensive thing the server does, so the
 * blurred picture is kept. It is recomputed only after the scene behind
 * the panel changed; the pointer passing over the panel, or the panel
 * redrawing itself, reuses it.
 */
static void draw_frosted(struct window *window, struct gfx_rect area)
{
    if (gfx_rect_is_empty(area)) {
        return;
    }
    /* A refresh needs the scene behind the whole panel to be composed,
     * which render_frame() arranged by growing the damage. */
    if (!window->backdrop_valid && !refresh_backdrop(window, area)) {
        gfx_blend(&back_buffer, window->x, window->y, &window->buffer.surface,
                  gfx_surface_bounds(&window->buffer.surface), 255);
        return;
    }

    const struct gfx_surface *panel = &window->buffer.surface;
    struct gfx_rect visible = gfx_rect_intersect(area, back_buffer.clip);
    for (int y = visible.y; y < visible.y + visible.height; y++) {
        const uint32_t *panel_row = panel->pixels + (size_t)(y - window->y) * (size_t)panel->stride;
        const uint32_t *blurred_row = window->backdrop.pixels + (size_t)(y - area.y) * (size_t)window->backdrop.stride;
        uint32_t *row = back_buffer.pixels + (size_t)y * (size_t)back_buffer.stride;

        for (int x = visible.x; x < visible.x + visible.width; x++) {
            uint32_t panel_pixel = panel_row[x - window->x];
            uint32_t alpha = panel_pixel >> 24;
            if (alpha == 0) {
                continue; /* outside the panel's shape: leave the scene sharp */
            }
            /* Fade the blur in over the panel's soft edge instead of
             * switching it on at the first slightly opaque pixel. */
            uint32_t blur_weight = alpha >= 64 ? 255 : alpha * 4;
            uint32_t behind = mix(row[x], blurred_row[x - area.x], blur_weight);
            row[x] = blend_over(behind, panel_pixel);
        }
    }
}

static void draw_window(struct window *window)
{
    if (window->kind == GUI_WINDOW_NORMAL) {
        decor_draw(&back_buffer, window, window == focused_window, true);
        decor_draw_content(&back_buffer, window);
        return;
    }
    struct gfx_rect source = gfx_surface_bounds(&window->buffer.surface);
    if (window->flags & GUI_CREATE_BLUR_BEHIND) {
        struct gfx_rect frosted = blur_rect(window);
        /* Parts of the window outside the frosted rectangle (for the dock:
         * icons growing above it) are blended normally. */
        struct gfx_rect saved_clip = back_buffer.clip;
        draw_frosted(window, frosted);
        for (int band = 0; band < 2; band++) {
            struct gfx_rect outside = window_content_rect(window);
            if (band == 0) {
                outside.height = frosted.y - outside.y;
            } else {
                outside.height = outside.y + outside.height - (frosted.y + frosted.height);
                outside.y = frosted.y + frosted.height;
            }
            gfx_set_clip(&back_buffer, gfx_rect_intersect(saved_clip, outside));
            gfx_blend(&back_buffer, window->x, window->y, &window->buffer.surface, source, 255);
        }
        back_buffer.clip = saved_clip;
    } else if (window->flags & GUI_CREATE_TRANSLUCENT) {
        gfx_blend(&back_buffer, window->x, window->y, &window->buffer.surface, source, 255);
    } else {
        /* Opaque content: a plain copy is much cheaper than blending. */
        gfx_blit(&back_buffer, window->x, window->y, &window->buffer.surface, source);
    }
}

/* The part of a window that certainly hides everything behind it. */
static struct gfx_rect opaque_rect(const struct window *window)
{
    if (window->flags & (GUI_CREATE_TRANSLUCENT | GUI_CREATE_BLUR_BEHIND)) {
        return gfx_rect_make(0, 0, 0, 0);
    }
    struct gfx_rect rect = window_content_rect(window);
    if (window->kind == GUI_WINDOW_NORMAL) {
        rect.height -= WINDOW_CORNER_RADIUS; /* the rounded corners show what is behind */
    }
    return rect;
}

/*
 * Finds where to start painting `area`. If some window covers all of it
 * with opaque content, nothing below that window can be seen there, so
 * painting it would be wasted work. This is the common case: an
 * application redrawing part of its own window.
 */
static int first_visible_layer(struct gfx_rect area)
{
    for (int i = window_stack_count - 1; i >= 0; i--) {
        const struct window *window = window_stack[i];
        if (!window_is_drawn(window)) {
            continue;
        }
        struct gfx_rect opaque = opaque_rect(window);
        if (area.x >= opaque.x && area.y >= opaque.y && area.x + area.width <= opaque.x + opaque.width
            && area.y + area.height <= opaque.y + opaque.height) {
            return i;
        }
    }
    return -1; /* nothing covers it: start with the background */
}

/* Composes everything that intersects `area` into the back buffer. */
static void compose(struct gfx_rect area)
{
    gfx_set_clip(&back_buffer, area);
    int first = first_visible_layer(area);
    if (first < 0) {
        gfx_clear(&back_buffer, area, BACKGROUND_COLOR);
        first = 0;
    }

    /* Painter's algorithm: bottom to top, each window over what is below. */
    for (int i = first; i < window_stack_count; i++) {
        struct window *window = window_stack[i];
        if (window_is_drawn(window) && !gfx_rect_is_empty(gfx_rect_intersect(window_visual_rect(window), area))) {
            draw_window(window);
        }
    }
    /* Animated stand-ins fly over everything, the dock included. */
    animations_draw(&back_buffer);
    cursor_draw(&back_buffer);

    gfx_reset_clip(&back_buffer);
}

/*
 * A frosted panel shows a blur of everything behind it, so a change behind
 * any part of it changes its whole frosted area, and recomputing the blur
 * needs the scene behind all of it. Grow damage that touches a panel whose
 * blur is out of date to cover that area.
 */
static void expand_damage_for_blur(struct gfx_rect *areas, int count)
{
    for (int i = 0; i < count; i++) {
        struct gfx_rect area = areas[i];
        for (int w = 0; w < window_stack_count; w++) {
            const struct window *window = window_stack[w];
            if (!window_is_shown(window) || (window->flags & GUI_CREATE_BLUR_BEHIND) == 0
                || window->backdrop_valid) {
                continue;
            }
            struct gfx_rect frosted = blur_rect(window);
            if (!gfx_rect_is_empty(gfx_rect_intersect(frosted, area))) {
                area = gfx_rect_union(area, frosted);
            }
        }
        /* Re-adding merges areas that now overlap. */
        damage_add_overlay(area);
    }
}

void render_frame(void)
{
    struct gfx_rect areas[DAMAGE_MAX_RECTS];
    int count = damage_take(areas, DAMAGE_MAX_RECTS);
    expand_damage_for_blur(areas, count);
    count = damage_take(areas, DAMAGE_MAX_RECTS);

    for (int i = 0; i < count; i++) {
        compose(areas[i]);
    }
    /* Copy to the display only after every area is composed, to keep the
     * time during which the display is partly updated as short as possible. */
    for (int i = 0; i < count; i++) {
        gfx_blit(&screen, areas[i].x, areas[i].y, &back_buffer, areas[i]);
    }
}
