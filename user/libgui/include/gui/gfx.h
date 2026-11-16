#ifndef GUI_GFX_H
#define GUI_GFX_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/*
 * 2D drawing in software.
 *
 * Colours given to drawing functions are 0xAARRGGBB with ordinary
 * ("straight") alpha: GFX_RGBA(255, 0, 0, 128) is half-transparent red.
 *
 * Pixels stored in a surface use PREMULTIPLIED alpha: the colour channels
 * are already multiplied by alpha, so that same half-transparent red is
 * stored as (128, 0, 0, 128). Blending one surface over another then needs
 * one multiplication per channel instead of two, and transparent edges do
 * not pick up colour fringes. The drawing functions convert for you.
 */
typedef uint32_t gfx_color;

#define GFX_RGBA(r, g, b, a) \
    (((gfx_color)(a) << 24) | ((gfx_color)(r) << 16) | ((gfx_color)(g) << 8) | (gfx_color)(b))
#define GFX_RGB(r, g, b) GFX_RGBA(r, g, b, 255)

#define GFX_ALPHA(color) (((color) >> 24) & 0xFF)
#define GFX_RED(color)   (((color) >> 16) & 0xFF)
#define GFX_GREEN(color) (((color) >> 8) & 0xFF)
#define GFX_BLUE(color)  ((color) & 0xFF)

/* The same colour with a different alpha. */
#define GFX_WITH_ALPHA(color, a) (((color) & 0x00FFFFFFu) | ((gfx_color)(a) << 24))

#define GFX_BLACK       GFX_RGB(0, 0, 0)
#define GFX_WHITE       GFX_RGB(255, 255, 255)
#define GFX_TRANSPARENT GFX_RGBA(0, 0, 0, 0)

struct gfx_rect {
    int x, y;
    int width, height;
};

static inline struct gfx_rect gfx_rect_make(int x, int y, int width, int height)
{
    return (struct gfx_rect){ x, y, width, height };
}

static inline bool gfx_rect_is_empty(struct gfx_rect rect)
{
    return rect.width <= 0 || rect.height <= 0;
}

static inline bool gfx_rect_contains(struct gfx_rect rect, int x, int y)
{
    return x >= rect.x && y >= rect.y && x < rect.x + rect.width && y < rect.y + rect.height;
}

/* The area both rectangles cover; empty (zero size) if they do not touch. */
struct gfx_rect gfx_rect_intersect(struct gfx_rect a, struct gfx_rect b);
/* The smallest rectangle containing both. An empty rectangle is ignored. */
struct gfx_rect gfx_rect_union(struct gfx_rect a, struct gfx_rect b);
/* The rectangle grown by `amount` on every side (shrunk if negative). */
struct gfx_rect gfx_rect_inset(struct gfx_rect rect, int amount);

/*
 * A rectangle of pixels to draw into. `stride` is the distance between the
 * starts of two rows, in pixels; it can be larger than `width` (the screen)
 * or describe a window into a bigger image. All drawing is limited to
 * `clip`.
 */
struct gfx_surface {
    uint32_t *pixels;
    int width, height;
    int stride;
    struct gfx_rect clip;
    bool owns_pixels;
};

/* Wraps memory the caller provides. */
void gfx_surface_init(struct gfx_surface *surface, uint32_t *pixels, int width, int height, int stride);
/* Allocates a zeroed (fully transparent) surface; false if out of memory. */
bool gfx_surface_create(struct gfx_surface *surface, int width, int height);
void gfx_surface_destroy(struct gfx_surface *surface);

static inline struct gfx_rect gfx_surface_bounds(const struct gfx_surface *surface)
{
    return gfx_rect_make(0, 0, surface->width, surface->height);
}

/* Restricts drawing to `clip` (intersected with the surface). */
void gfx_set_clip(struct gfx_surface *surface, struct gfx_rect clip);
void gfx_reset_clip(struct gfx_surface *surface);

/* ------------------------------------------------------------- shapes */

/* Replaces pixels, alpha included: clearing to GFX_TRANSPARENT makes a
 * hole. Everything below blends over what is already there instead. */
void gfx_clear(struct gfx_surface *surface, struct gfx_rect rect, gfx_color color);

void gfx_fill_rect(struct gfx_surface *surface, struct gfx_rect rect, gfx_color color);
/* One-pixel outline just inside `rect`. */
void gfx_stroke_rect(struct gfx_surface *surface, struct gfx_rect rect, gfx_color color);
/* Corners are quarter circles with anti-aliased edges. */
void gfx_fill_rounded_rect(struct gfx_surface *surface, struct gfx_rect rect, int radius, gfx_color color);
void gfx_fill_circle(struct gfx_surface *surface, int center_x, int center_y, int radius, gfx_color color);
/* Vertical gradient from `top` to `bottom`. */
void gfx_fill_gradient(struct gfx_surface *surface, struct gfx_rect rect, gfx_color top, gfx_color bottom);

/* ------------------------------------------------------------ surfaces */

/* Copies `source_rect` of `source` to (x, y), replacing what is there. */
void gfx_blit(struct gfx_surface *surface, int x, int y, const struct gfx_surface *source,
              struct gfx_rect source_rect);
/* Blends it over what is there, using the source's alpha scaled by
 * `opacity` (0-255). */
void gfx_blend(struct gfx_surface *surface, int x, int y, const struct gfx_surface *source,
               struct gfx_rect source_rect, unsigned opacity);

/*
 * Blends `source_rect` of `source`, stretched or shrunk to fill
 * `target_rect`, with smooth (bilinear) sampling. Used for animations and
 * for drawing one image at several sizes.
 */
void gfx_blend_scaled(struct gfx_surface *surface, struct gfx_rect target_rect, const struct gfx_surface *source,
                      struct gfx_rect source_rect, unsigned opacity);

/* -------------------------------------------------------------- effects */

/*
 * Blurs the pixels of `rect` in place. `radius` is the distance over which
 * a pixel is smeared; `passes` box blurs are applied in a row: one is
 * cheap and blocky, three are close to a true Gaussian blur.
 */
void gfx_box_blur(struct gfx_surface *surface, struct gfx_rect rect, int radius, int passes);

/* ---------------------------------------------------------------- text */

struct gfx_font;

/* Loads a font atlas file produced by tools/mkfont.py; NULL on failure. */
struct gfx_font *gfx_font_load(const char *path);
void gfx_font_destroy(struct gfx_font *font);

/* Vertical metrics in pixels: distance from the baseline up to the top of
 * tall letters, down to the bottom of descenders, and between baselines. */
int gfx_font_ascent(const struct gfx_font *font);
int gfx_font_descent(const struct gfx_font *font);
int gfx_font_line_height(const struct gfx_font *font);

/* Width in pixels of UTF-8 `text` (up to `length` bytes, or to its end if
 * `length` is negative). */
int gfx_text_width(const struct gfx_font *font, const char *text, int length);

/* Draws UTF-8 text with its baseline at `baseline_y`, starting at `x`.
 * Returns the x position after the last character. */
int gfx_draw_text(struct gfx_surface *surface, const struct gfx_font *font, int x, int baseline_y,
                  const char *text, int length, gfx_color color);

#endif
