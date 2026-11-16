#ifndef GUI_UI_H
#define GUI_UI_H

#include <gui/gfx.h>

/*
 * Shared look of the applications: fonts, colours and a few standard
 * controls. These are drawing helpers, not objects: an application keeps
 * its own state (is the switch on? which button is pressed?), asks these
 * functions to draw it, and tests clicks against the rectangles it used.
 */

/* ---------------------------------------------------------------- fonts */

enum ui_font_id {
    UI_FONT_SMALL,    /* Inter Regular 11 */
    UI_FONT_BODY,     /* Inter Regular 13 */
    UI_FONT_MEDIUM,   /* Inter Medium 13 */
    UI_FONT_STRONG,   /* Inter SemiBold 13 */
    UI_FONT_HEADING,  /* Inter SemiBold 15 */
    UI_FONT_LARGE,    /* Inter Regular 20 */
    UI_FONT_TITLE,    /* Inter SemiBold 28 */
    UI_FONT_MONO,     /* JetBrains Mono Regular 13 */
    UI_FONT_COUNT
};

/* Fonts are loaded from /fonts on first use and kept for the life of the
 * program. Returns NULL if the file is missing. */
struct gfx_font *ui_font(enum ui_font_id id);

/* --------------------------------------------------------------- colours */

#define UI_COLOR_WINDOW      GFX_RGB(246, 246, 248)
#define UI_COLOR_PANEL       GFX_RGB(255, 255, 255)
#define UI_COLOR_TEXT        GFX_RGB(28, 28, 34)
#define UI_COLOR_SECONDARY   GFX_RGB(120, 120, 130)
#define UI_COLOR_SEPARATOR   GFX_RGB(222, 222, 228)
#define UI_COLOR_ACCENT      GFX_RGB(0, 122, 255)
#define UI_COLOR_CONTROL     GFX_RGB(232, 232, 237)
#define UI_COLOR_CONTROL_PRESSED GFX_RGB(212, 212, 218)
#define UI_COLOR_SELECTION   GFX_RGB(214, 230, 255)

/* ------------------------------------------------------------------ text */

/* The baseline that centres one line of `font` vertically in `rect`. */
int ui_baseline(const struct gfx_font *font, struct gfx_rect rect);
/* One line of text, vertically centred, starting at the rectangle's left
 * edge and clipped to it. */
void ui_text(struct gfx_surface *surface, enum ui_font_id font, struct gfx_rect rect, const char *text,
             gfx_color color);
void ui_text_centered(struct gfx_surface *surface, enum ui_font_id font, struct gfx_rect rect, const char *text,
                      gfx_color color);
void ui_text_right(struct gfx_surface *surface, enum ui_font_id font, struct gfx_rect rect, const char *text,
                   gfx_color color);

/* -------------------------------------------------------------- controls */

#define UI_SWITCH_WIDTH 40
#define UI_SWITCH_HEIGHT 22

/* A push button; `primary` buttons use the accent colour. */
void ui_button(struct gfx_surface *surface, struct gfx_rect rect, const char *label, bool pressed, bool primary);
/* An on/off switch, UI_SWITCH_WIDTH x UI_SWITCH_HEIGHT pixels at rect's origin. */
void ui_switch(struct gfx_surface *surface, struct gfx_rect rect, bool on);
/* A row of mutually exclusive choices with one selected. */
void ui_segmented(struct gfx_surface *surface, struct gfx_rect rect, const char *const labels[], int count,
                  int selected);
/* The part of a segmented control that belongs to choice `index`. */
struct gfx_rect ui_segment_rect(struct gfx_rect rect, int count, int index);

#endif
