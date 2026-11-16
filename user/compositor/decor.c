/*
 * Window decorations: the title bar the server draws around application
 * windows, and the geometry that goes with it.
 *
 * Drawing decorations in the server (rather than in each application)
 * keeps them consistent, and lets a window be moved or closed even while
 * its application is busy or stuck.
 */
#include "compositor.h"

#define CORNER_RADIUS 10
#define BUTTON_RADIUS 6
#define BUTTON_FIRST_X 18 /* centre of the leftmost button, from the frame's left edge */
#define BUTTON_SPACING 20
#define BUTTON_HIT_SLACK 2 /* extra pixels around a button that still count */

#define COLOR_TITLE_FOCUSED   GFX_RGB(232, 232, 237)
#define COLOR_TITLE_UNFOCUSED GFX_RGB(244, 244, 247)
#define COLOR_TITLE_SEPARATOR GFX_RGB(200, 200, 205)
#define COLOR_TEXT_FOCUSED    GFX_RGB(50, 50, 56)
#define COLOR_TEXT_UNFOCUSED  GFX_RGB(160, 160, 168)
#define COLOR_BUTTON_INACTIVE GFX_RGB(206, 206, 211)
#define COLOR_BUTTON_CLOSE    GFX_RGB(255, 95, 87)
#define COLOR_BUTTON_MINIMIZE GFX_RGB(254, 188, 46)
#define COLOR_BUTTON_ZOOM     GFX_RGB(40, 200, 64)

static struct gfx_font *title_font;

void decor_init(void)
{
    /* Without the font, windows are still usable: they just lack titles. */
    title_font = gfx_font_load("/fonts/Inter-SemiBold-13.sdf");
}

static bool is_decorated(const struct window *window)
{
    return window->kind == GUI_WINDOW_NORMAL;
}

struct gfx_rect window_content_rect(const struct window *window)
{
    return gfx_rect_make(window->x, window->y, window->width, window->height);
}

struct gfx_rect window_frame_rect(const struct window *window)
{
    if (!is_decorated(window)) {
        return window_content_rect(window);
    }
    return gfx_rect_make(window->x, window->y - TITLE_BAR_HEIGHT, window->width, window->height + TITLE_BAR_HEIGHT);
}

static void button_center(const struct window *window, int index, int *x, int *y)
{
    *x = window->x + BUTTON_FIRST_X + index * BUTTON_SPACING;
    *y = window->y - TITLE_BAR_HEIGHT / 2;
}

static bool on_button(const struct window *window, int index, int x, int y)
{
    int center_x, center_y;
    button_center(window, index, &center_x, &center_y);
    int dx = x - center_x, dy = y - center_y;
    int reach = BUTTON_RADIUS + BUTTON_HIT_SLACK;
    return dx * dx + dy * dy <= reach * reach;
}

enum hit_area window_hit_test(const struct window *window, int x, int y)
{
    if (gfx_rect_contains(window_content_rect(window), x, y)) {
        return HIT_CONTENT;
    }
    if (!is_decorated(window) || !gfx_rect_contains(window_frame_rect(window), x, y)) {
        return HIT_NONE;
    }
    if (on_button(window, 0, x, y)) {
        return HIT_CLOSE_BUTTON;
    }
    if (on_button(window, 1, x, y)) {
        return HIT_MINIMIZE_BUTTON;
    }
    if (on_button(window, 2, x, y)) {
        return HIT_ZOOM_BUTTON;
    }
    return HIT_TITLE_BAR;
}

static void draw_buttons(struct gfx_surface *target, const struct window *window, bool focused)
{
    static const gfx_color colors[3] = { COLOR_BUTTON_CLOSE, COLOR_BUTTON_MINIMIZE, COLOR_BUTTON_ZOOM };
    for (int i = 0; i < 3; i++) {
        int x, y;
        button_center(window, i, &x, &y);
        gfx_fill_circle(target, x, y, BUTTON_RADIUS, focused ? colors[i] : COLOR_BUTTON_INACTIVE);
    }
}

static void draw_title(struct gfx_surface *target, const struct window *window, struct gfx_rect bar, bool focused)
{
    if (title_font == NULL || window->title[0] == '\0') {
        return;
    }
    /* Centred, but never under the buttons. */
    int text_width = gfx_text_width(title_font, window->title, -1);
    int left_limit = bar.x + BUTTON_FIRST_X + 2 * BUTTON_SPACING + BUTTON_RADIUS + 10;
    int x = bar.x + (bar.width - text_width) / 2;
    if (x < left_limit) {
        x = left_limit;
    }
    int baseline = bar.y + (bar.height + gfx_font_ascent(title_font) - gfx_font_descent(title_font)) / 2;

    /* Clip to the bar so a long title cannot spill outside the window. */
    struct gfx_rect saved_clip = target->clip;
    gfx_set_clip(target, gfx_rect_intersect(saved_clip, gfx_rect_make(left_limit, bar.y, bar.x + bar.width - 8 - left_limit, bar.height)));
    gfx_draw_text(target, title_font, x, baseline, window->title, -1, focused ? COLOR_TEXT_FOCUSED : COLOR_TEXT_UNFOCUSED);
    target->clip = saved_clip;
}

void decor_draw(struct gfx_surface *target, const struct window *window, bool focused)
{
    if (!is_decorated(window)) {
        return;
    }
    struct gfx_rect bar = gfx_rect_make(window->x, window->y - TITLE_BAR_HEIGHT, window->width, TITLE_BAR_HEIGHT);
    gfx_color color = focused ? COLOR_TITLE_FOCUSED : COLOR_TITLE_UNFOCUSED;

    /* Rounded top corners, square bottom: a rounded rectangle taller than
     * the bar, with the lower part clipped away. */
    struct gfx_rect saved_clip = target->clip;
    gfx_set_clip(target, gfx_rect_intersect(saved_clip, bar));
    gfx_fill_rounded_rect(target, gfx_rect_make(bar.x, bar.y, bar.width, bar.height + CORNER_RADIUS), CORNER_RADIUS,
                          color);
    gfx_fill_rect(target, gfx_rect_make(bar.x, bar.y + bar.height - 1, bar.width, 1), COLOR_TITLE_SEPARATOR);
    target->clip = saved_clip;

    draw_buttons(target, window, focused);
    draw_title(target, window, bar, focused);
}
