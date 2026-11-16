#include <gui/ui.h>

#include <stddef.h>

static const char *const font_paths[UI_FONT_COUNT] = {
    [UI_FONT_SMALL] = "/fonts/Inter-Regular-11.sdf",
    [UI_FONT_BODY] = "/fonts/Inter-Regular-13.sdf",
    [UI_FONT_MEDIUM] = "/fonts/Inter-Medium-13.sdf",
    [UI_FONT_STRONG] = "/fonts/Inter-SemiBold-13.sdf",
    [UI_FONT_HEADING] = "/fonts/Inter-SemiBold-15.sdf",
    [UI_FONT_LARGE] = "/fonts/Inter-Regular-20.sdf",
    [UI_FONT_TITLE] = "/fonts/Inter-SemiBold-28.sdf",
    [UI_FONT_MONO] = "/fonts/JetBrainsMono-Regular-13.sdf",
};

static struct gfx_font *fonts[UI_FONT_COUNT];
static bool font_tried[UI_FONT_COUNT];

struct gfx_font *ui_font(enum ui_font_id id)
{
    /* Try each file once: a missing font should not be looked for again
     * on every redraw. */
    if (!font_tried[id]) {
        font_tried[id] = true;
        fonts[id] = gfx_font_load(font_paths[id]);
    }
    return fonts[id];
}

int ui_baseline(const struct gfx_font *font, struct gfx_rect rect)
{
    return rect.y + (rect.height + gfx_font_ascent(font) - gfx_font_descent(font)) / 2;
}

/* Draws text at `x`, vertically centred in `rect`, never outside `rect`. */
static void draw_clipped(struct gfx_surface *surface, const struct gfx_font *font, struct gfx_rect rect, int x,
                         const char *text, gfx_color color)
{
    struct gfx_rect saved = surface->clip;
    gfx_set_clip(surface, gfx_rect_intersect(saved, rect));
    gfx_draw_text(surface, font, x, ui_baseline(font, rect), text, -1, color);
    surface->clip = saved;
}

void ui_text(struct gfx_surface *surface, enum ui_font_id id, struct gfx_rect rect, const char *text, gfx_color color)
{
    struct gfx_font *font = ui_font(id);
    if (font != NULL) {
        draw_clipped(surface, font, rect, rect.x, text, color);
    }
}

void ui_text_centered(struct gfx_surface *surface, enum ui_font_id id, struct gfx_rect rect, const char *text,
                      gfx_color color)
{
    struct gfx_font *font = ui_font(id);
    if (font != NULL) {
        draw_clipped(surface, font, rect, rect.x + (rect.width - gfx_text_width(font, text, -1)) / 2, text, color);
    }
}

void ui_text_right(struct gfx_surface *surface, enum ui_font_id id, struct gfx_rect rect, const char *text,
                   gfx_color color)
{
    struct gfx_font *font = ui_font(id);
    if (font != NULL) {
        draw_clipped(surface, font, rect, rect.x + rect.width - gfx_text_width(font, text, -1), text, color);
    }
}

void ui_button(struct gfx_surface *surface, struct gfx_rect rect, const char *label, bool pressed, bool primary)
{
    gfx_color fill = primary ? UI_COLOR_ACCENT : UI_COLOR_CONTROL;
    if (pressed) {
        fill = primary ? GFX_RGB(0, 100, 215) : UI_COLOR_CONTROL_PRESSED;
    }
    gfx_fill_rounded_rect(surface, rect, 6, fill);
    ui_text_centered(surface, UI_FONT_MEDIUM, rect, label, primary ? GFX_WHITE : UI_COLOR_TEXT);
}

void ui_switch(struct gfx_surface *surface, struct gfx_rect rect, bool on)
{
    struct gfx_rect track = gfx_rect_make(rect.x, rect.y, UI_SWITCH_WIDTH, UI_SWITCH_HEIGHT);
    int radius = UI_SWITCH_HEIGHT / 2;
    gfx_fill_rounded_rect(surface, track, radius, on ? GFX_RGB(52, 199, 89) : GFX_RGB(200, 200, 206));
    /* The knob sits at the end that corresponds to the state. */
    int knob_x = on ? track.x + track.width - radius : track.x + radius;
    gfx_fill_circle(surface, knob_x, track.y + radius, radius - 2, GFX_WHITE);
}

struct gfx_rect ui_segment_rect(struct gfx_rect rect, int count, int index)
{
    int left = rect.x + rect.width * index / count;
    int right = rect.x + rect.width * (index + 1) / count;
    return gfx_rect_make(left, rect.y, right - left, rect.height);
}

void ui_segmented(struct gfx_surface *surface, struct gfx_rect rect, const char *const labels[], int count,
                  int selected)
{
    gfx_fill_rounded_rect(surface, rect, 7, UI_COLOR_CONTROL);
    for (int i = 0; i < count; i++) {
        struct gfx_rect segment = ui_segment_rect(rect, count, i);
        if (i == selected) {
            gfx_fill_rounded_rect(surface, gfx_rect_inset(segment, -2), 5, UI_COLOR_PANEL);
        }
        ui_text_centered(surface, i == selected ? UI_FONT_MEDIUM : UI_FONT_BODY, segment, labels[i], UI_COLOR_TEXT);
    }
}
