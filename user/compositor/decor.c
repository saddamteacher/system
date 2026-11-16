/*
 * Window decorations: the shadow, title bar and rounded corners the server
 * draws around application windows, and the geometry that goes with them.
 *
 * Drawing decorations in the server (rather than in each application)
 * keeps them consistent, and lets a window be moved or closed even while
 * its application is busy or stuck.
 */
#include "compositor.h"

#include <gui/pixel.h>
#include <math.h>

#define BUTTON_RADIUS 6
#define BUTTON_FIRST_X 18 /* centre of the leftmost button, from the frame's left edge */
#define BUTTON_SPACING 20
#define BUTTON_HIT_SLACK 2 /* extra pixels around a button that still count */

/* How close to the right or bottom edge a press starts a resize. */
#define RESIZE_EDGE 6
#define RESIZE_CORNER 16

#define COLOR_TITLE_FOCUSED   GFX_RGB(232, 232, 237)
#define COLOR_TITLE_UNFOCUSED GFX_RGB(244, 244, 247)
#define COLOR_TITLE_SEPARATOR GFX_RGB(200, 200, 205)
#define COLOR_TEXT_FOCUSED    GFX_RGB(50, 50, 56)
#define COLOR_TEXT_UNFOCUSED  GFX_RGB(160, 160, 168)
#define COLOR_BUTTON_INACTIVE GFX_RGB(206, 206, 211)
#define COLOR_BUTTON_CLOSE    GFX_RGB(255, 95, 87)
#define COLOR_BUTTON_MINIMIZE GFX_RGB(254, 188, 46)
#define COLOR_BUTTON_ZOOM     GFX_RGB(40, 200, 64)

/*
 * The shadow is a blurred, dark copy of the window's outline, slightly
 * larger and shifted down, drawn beneath it.
 *
 * Blurring a window-sized shape every frame would be far too slow. But the
 * blurred outline of a rounded rectangle looks the same along each straight
 * edge, and the four corners do not depend on the window's size. So one
 * small square is blurred once, at start-up, and then stretched: corners
 * are copied, edges repeat a single row or column ("nine-slice").
 */
#define SHADOW_SPREAD 22    /* how far the shadow reaches beyond the frame */
#define SHADOW_BLUR_RADIUS 6
#define SHADOW_BLUR_PASSES 3
#define SHADOW_OFFSET_Y 7
#define SHADOW_OPACITY_FOCUSED 125
#define SHADOW_OPACITY_UNFOCUSED 70
/* Distance from the texture's edge to where a straight edge begins: the
 * spread, the corner, and the reach of the blur. */
#define SHADOW_EDGE (SHADOW_SPREAD + WINDOW_CORNER_RADIUS + SHADOW_BLUR_RADIUS * SHADOW_BLUR_PASSES)
#define SHADOW_TEXTURE_SIZE (2 * SHADOW_EDGE + 1)

static struct gfx_font *title_font;
static struct gfx_surface shadow_texture;
static bool shadow_ready;
/* Coverage (0-255) of each pixel of a bottom-left window corner. */
static uint8_t corner_coverage[WINDOW_CORNER_RADIUS][WINDOW_CORNER_RADIUS];

static void build_shadow_texture(void)
{
    if (!gfx_surface_create(&shadow_texture, SHADOW_TEXTURE_SIZE, SHADOW_TEXTURE_SIZE)) {
        return; /* windows are simply drawn without shadows */
    }
    int inner = SHADOW_TEXTURE_SIZE - 2 * SHADOW_SPREAD;
    gfx_fill_rounded_rect(&shadow_texture, gfx_rect_make(SHADOW_SPREAD, SHADOW_SPREAD, inner, inner),
                          WINDOW_CORNER_RADIUS, GFX_BLACK);
    gfx_box_blur(&shadow_texture, gfx_surface_bounds(&shadow_texture), SHADOW_BLUR_RADIUS, SHADOW_BLUR_PASSES);
    shadow_ready = true;
}

static void build_corner_coverage(void)
{
    /* Row 0 is the top of the corner square, column 0 its left edge; the
     * circle's centre is the square's top-right corner. */
    float radius = (float)WINDOW_CORNER_RADIUS;
    for (int row = 0; row < WINDOW_CORNER_RADIUS; row++) {
        for (int column = 0; column < WINDOW_CORNER_RADIUS; column++) {
            float dx = radius - ((float)column + 0.5f);
            float dy = (float)row + 0.5f;
            float inside = radius + 0.5f - sqrtf(dx * dx + dy * dy);
            inside = inside < 0.0f ? 0.0f : (inside > 1.0f ? 1.0f : inside);
            corner_coverage[row][column] = (uint8_t)(inside * 255.0f + 0.5f);
        }
    }
}

void decor_init(void)
{
    /* Without the font, windows are still usable: they just lack titles. */
    title_font = gfx_font_load("/fonts/Inter-SemiBold-13.sdf");
    build_shadow_texture();
    build_corner_coverage();
}

/* ------------------------------------------------------------ geometry */

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

static struct gfx_rect shadow_rect(struct gfx_rect frame)
{
    struct gfx_rect rect = gfx_rect_inset(frame, SHADOW_SPREAD);
    rect.y += SHADOW_OFFSET_Y;
    return rect;
}

struct gfx_rect window_visual_rect(const struct window *window)
{
    struct gfx_rect frame = window_frame_rect(window);
    return is_decorated(window) ? gfx_rect_union(frame, shadow_rect(frame)) : frame;
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

unsigned resize_edges_at(const struct window *window, int x, int y)
{
    struct gfx_rect frame = window_frame_rect(window);
    if (!is_decorated(window) || window->zoomed || !gfx_rect_contains(frame, x, y)) {
        return 0;
    }
    int from_right = frame.x + frame.width - 1 - x;
    int from_bottom = frame.y + frame.height - 1 - y;
    if (from_right < RESIZE_CORNER && from_bottom < RESIZE_CORNER) {
        return RESIZE_RIGHT | RESIZE_BOTTOM;
    }
    unsigned edges = 0;
    /* The right edge of the title bar belongs to dragging, not resizing. */
    if (from_right < RESIZE_EDGE && y >= window->y) {
        edges |= RESIZE_RIGHT;
    }
    if (from_bottom < RESIZE_EDGE) {
        edges |= RESIZE_BOTTOM;
    }
    return edges;
}

enum hit_area window_hit_test(const struct window *window, int x, int y)
{
    if (!is_decorated(window)) {
        struct gfx_rect area = window_content_rect(window);
        if (window->has_input_region) {
            area = gfx_rect_make(window->x + window->input_region.x, window->y + window->input_region.y,
                                 window->input_region.width, window->input_region.height);
        }
        return gfx_rect_contains(area, x, y) ? HIT_CONTENT : HIT_NONE;
    }
    if (!gfx_rect_contains(window_frame_rect(window), x, y)) {
        return HIT_NONE;
    }
    if (resize_edges_at(window, x, y) != 0) {
        return HIT_RESIZE;
    }
    if (y >= window->y) {
        return HIT_CONTENT;
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

/* ------------------------------------------------------------- drawing */

/* Maps a position along a stretched side of length `length` to a position
 * in the texture: the two ends map one to one, everything between maps to
 * the texture's middle line. */
static int nine_slice(int position, int length)
{
    if (position < SHADOW_EDGE) {
        return position;
    }
    if (position >= length - SHADOW_EDGE) {
        return SHADOW_TEXTURE_SIZE - (length - position);
    }
    return SHADOW_EDGE;
}

static void draw_shadow(struct gfx_surface *target, struct gfx_rect frame, bool focused)
{
    struct gfx_rect bounds = shadow_rect(frame);
    struct gfx_rect visible = gfx_rect_intersect(bounds, target->clip);
    if (!shadow_ready || gfx_rect_is_empty(visible) || bounds.width < SHADOW_TEXTURE_SIZE
        || bounds.height < SHADOW_TEXTURE_SIZE) {
        return;
    }
    /* The window itself is opaque and drawn afterwards, so the shadow
     * under it is never seen. Skipping it is most of the shadow's area.
     * (Shrunk by the corner radius, where the window is not opaque.) */
    struct gfx_rect hidden = gfx_rect_inset(frame, -WINDOW_CORNER_RADIUS);
    uint32_t opacity = focused ? SHADOW_OPACITY_FOCUSED : SHADOW_OPACITY_UNFOCUSED;

    for (int y = visible.y; y < visible.y + visible.height; y++) {
        const uint32_t *texture_row =
            shadow_texture.pixels + (size_t)nine_slice(y - bounds.y, bounds.height) * SHADOW_TEXTURE_SIZE;
        uint32_t *row = target->pixels + (size_t)y * (size_t)target->stride;
        bool row_crosses_hidden = y >= hidden.y && y < hidden.y + hidden.height;

        for (int x = visible.x; x < visible.x + visible.width; x++) {
            if (row_crosses_hidden && x >= hidden.x && x < hidden.x + hidden.width) {
                x = hidden.x + hidden.width - 1; /* jump across the window */
                continue;
            }
            uint32_t texel = texture_row[nine_slice(x - bounds.x, bounds.width)];
            if ((texel >> 24) != 0) {
                row[x] = blend_over(row[x], scale_pixel(texel, opacity));
            }
        }
    }
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
    struct gfx_rect text_area = gfx_rect_make(left_limit, bar.y, bar.x + bar.width - 8 - left_limit, bar.height);
    gfx_set_clip(target, gfx_rect_intersect(saved_clip, text_area));
    gfx_draw_text(target, title_font, x, baseline, window->title, -1,
                  focused ? COLOR_TEXT_FOCUSED : COLOR_TEXT_UNFOCUSED);
    target->clip = saved_clip;
}

void decor_draw(struct gfx_surface *target, const struct window *window, bool focused, bool with_shadow)
{
    if (!is_decorated(window)) {
        return;
    }
    if (with_shadow) {
        draw_shadow(target, window_frame_rect(window), focused);
    }

    struct gfx_rect bar = gfx_rect_make(window->x, window->y - TITLE_BAR_HEIGHT, window->width, TITLE_BAR_HEIGHT);
    gfx_color color = focused ? COLOR_TITLE_FOCUSED : COLOR_TITLE_UNFOCUSED;

    /* Rounded top corners, square bottom: a rounded rectangle taller than
     * the bar, with the lower part clipped away. */
    struct gfx_rect saved_clip = target->clip;
    gfx_set_clip(target, gfx_rect_intersect(saved_clip, bar));
    gfx_fill_rounded_rect(target, gfx_rect_make(bar.x, bar.y, bar.width, bar.height + WINDOW_CORNER_RADIUS),
                          WINDOW_CORNER_RADIUS, color);
    gfx_fill_rect(target, gfx_rect_make(bar.x, bar.y + bar.height - 1, bar.width, 1), COLOR_TITLE_SEPARATOR);
    target->clip = saved_clip;

    draw_buttons(target, window, focused);
    draw_title(target, window, bar, focused);
}

/* Blends one content pixel into the target with the given coverage. */
static void blend_corner_pixel(struct gfx_surface *target, int x, int y, uint32_t source, uint32_t coverage)
{
    if (coverage == 0 || !gfx_rect_contains(target->clip, x, y)) {
        return;
    }
    uint32_t *pixel = target->pixels + (size_t)y * (size_t)target->stride + x;
    /* Window content is opaque whatever its alpha byte says. */
    *pixel = blend_over(*pixel, scale_pixel(source | 0xFF000000u, coverage));
}

void decor_draw_content(struct gfx_surface *target, const struct window *window)
{
    const struct gfx_surface *content = &window->buffer.surface;
    int radius = WINDOW_CORNER_RADIUS;
    /* The buffer can be a different size than the window for a moment
     * while a resize is in progress; never read past either. */
    int width = content->width < window->width ? content->width : window->width;
    int height = content->height < window->height ? content->height : window->height;

    if (!is_decorated(window) || width < 2 * radius || height < radius) {
        gfx_blit(target, window->x, window->y, content, gfx_rect_make(0, 0, width, height));
        return;
    }
    /* Everything except the two bottom corner squares is a plain copy. */
    gfx_blit(target, window->x, window->y, content, gfx_rect_make(0, 0, width, height - radius));
    gfx_blit(target, window->x + radius, window->y + height - radius, content,
             gfx_rect_make(radius, height - radius, width - 2 * radius, radius));

    for (int row = 0; row < radius; row++) {
        int source_y = height - radius + row;
        const uint32_t *source_row = content->pixels + (size_t)source_y * (size_t)content->stride;
        for (int column = 0; column < radius; column++) {
            uint32_t coverage = corner_coverage[row][column];
            blend_corner_pixel(target, window->x + column, window->y + source_y, source_row[column], coverage);
            /* The right corner is the mirror image of the left. */
            blend_corner_pixel(target, window->x + width - 1 - column, window->y + source_y,
                               source_row[width - 1 - column], coverage);
        }
    }
}
