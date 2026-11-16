/*
 * Composition: building the picture on screen out of the windows.
 *
 * Frames are composed in a back buffer and then copied to the display, so
 * the display never shows a partly drawn state. Only damaged rectangles
 * are composed and copied.
 */
#include "compositor.h"

#include <display.h>
#include <stdio.h>

struct gfx_surface screen;
struct gfx_surface back_buffer;

/* Shown where no window covers the screen. */
#define BACKGROUND_TOP    GFX_RGB(52, 62, 96)
#define BACKGROUND_BOTTOM GFX_RGB(118, 92, 128)

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

static void draw_window(const struct window *window)
{
    decor_draw(&back_buffer, window, window == focused_window);

    struct gfx_rect source = gfx_surface_bounds(&window->surface);
    if (window->flags & GUI_CREATE_TRANSLUCENT) {
        gfx_blend(&back_buffer, window->x, window->y, &window->surface, source, 255);
    } else {
        /* Opaque content: a plain copy is much cheaper than blending, and
         * it ignores whatever the client left in the alpha channel. */
        gfx_blit(&back_buffer, window->x, window->y, &window->surface, source);
    }
}

/* Composes everything that intersects `area` into the back buffer. */
static void compose(struct gfx_rect area)
{
    gfx_set_clip(&back_buffer, area);

    gfx_fill_gradient(&back_buffer, gfx_surface_bounds(&back_buffer), BACKGROUND_TOP, BACKGROUND_BOTTOM);

    /* Painter's algorithm: bottom to top, each window over what is below. */
    for (int i = 0; i < window_stack_count; i++) {
        const struct window *window = window_stack[i];
        if (window->visible && !gfx_rect_is_empty(gfx_rect_intersect(window_frame_rect(window), area))) {
            draw_window(window);
        }
    }
    cursor_draw(&back_buffer);

    gfx_reset_clip(&back_buffer);
}

void render_frame(void)
{
    struct gfx_rect areas[DAMAGE_MAX_RECTS];
    int count = damage_take(areas, DAMAGE_MAX_RECTS);

    for (int i = 0; i < count; i++) {
        compose(areas[i]);
    }
    /* Copy to the display only after every area is composed, to keep the
     * time during which the display is partly updated as short as possible. */
    for (int i = 0; i < count; i++) {
        gfx_blit(&screen, areas[i].x, areas[i].y, &back_buffer, areas[i]);
    }
}
