#include <gui/gfx.h>

#include <gui/pixel.h>
#include <stdlib.h>

/*
 * Box blur: every pixel becomes the average of the pixels within `radius`
 * of it. Done naively that costs (2 * radius + 1) reads per pixel; here it
 * costs two, whatever the radius. The trick is a running sum: sliding the
 * window one pixel to the right adds one pixel and drops one.
 *
 * A two-dimensional box blur is the same as blurring every row and then
 * every column, which is what gfx_box_blur does.
 */

struct sums {
    uint32_t alpha, red, green, blue;
};

static void sums_add(struct sums *sums, uint32_t pixel)
{
    sums->alpha += pixel >> 24;
    sums->red += (pixel >> 16) & 0xFF;
    sums->green += (pixel >> 8) & 0xFF;
    sums->blue += pixel & 0xFF;
}

static void sums_remove(struct sums *sums, uint32_t pixel)
{
    sums->alpha -= pixel >> 24;
    sums->red -= (pixel >> 16) & 0xFF;
    sums->green -= (pixel >> 8) & 0xFF;
    sums->blue -= pixel & 0xFF;
}

/*
 * Blurs `count` pixels that are `stride` apart, starting at `line`. The
 * same routine handles rows (stride 1) and columns (stride = row length).
 * `scratch` must hold `count` pixels. Past the ends, the edge pixel is
 * repeated, so edges do not darken.
 */
static void blur_line(uint32_t *line, int count, int stride, int radius, uint32_t *scratch)
{
    for (int i = 0; i < count; i++) {
        scratch[i] = line[(size_t)i * (size_t)stride];
    }
    uint32_t window = (uint32_t)(2 * radius + 1);
    /* Multiply by a fixed-point reciprocal instead of dividing per pixel. */
    uint32_t reciprocal = (1u << 16) / window;

    struct sums sums = { 0, 0, 0, 0 };
    for (int offset = -radius; offset <= radius; offset++) {
        int index = offset < 0 ? 0 : (offset >= count ? count - 1 : offset);
        sums_add(&sums, scratch[index]);
    }
    for (int i = 0; i < count; i++) {
        uint32_t alpha = (sums.alpha * reciprocal + (1u << 15)) >> 16;
        uint32_t red = (sums.red * reciprocal + (1u << 15)) >> 16;
        uint32_t green = (sums.green * reciprocal + (1u << 15)) >> 16;
        uint32_t blue = (sums.blue * reciprocal + (1u << 15)) >> 16;
        line[(size_t)i * (size_t)stride] = (alpha << 24) | (red << 16) | (green << 8) | blue;

        int leaving = i - radius;
        int entering = i + radius + 1;
        sums_remove(&sums, scratch[leaving < 0 ? 0 : leaving]);
        sums_add(&sums, scratch[entering >= count ? count - 1 : entering]);
    }
}

void gfx_box_blur(struct gfx_surface *surface, struct gfx_rect rect, int radius, int passes)
{
    rect = gfx_rect_intersect(rect, gfx_surface_bounds(surface));
    if (gfx_rect_is_empty(rect) || radius <= 0 || passes <= 0) {
        return;
    }
    int longest = rect.width > rect.height ? rect.width : rect.height;
    uint32_t *scratch = malloc((size_t)longest * sizeof(uint32_t));
    if (scratch == NULL) {
        return; /* an effect, not a necessity: leave the image sharp */
    }
    uint32_t *origin = surface->pixels + (size_t)rect.y * (size_t)surface->stride + rect.x;

    for (int pass = 0; pass < passes; pass++) {
        for (int y = 0; y < rect.height; y++) {
            blur_line(origin + (size_t)y * (size_t)surface->stride, rect.width, 1, radius, scratch);
        }
        for (int x = 0; x < rect.width; x++) {
            blur_line(origin + x, rect.height, surface->stride, radius, scratch);
        }
    }
    free(scratch);
}

/* ---------------------------------------------------------------- scaling */

/* Positions in the source image are tracked in 16.16 fixed point: 16 bits
 * of whole pixels, 16 bits of fraction. */
#define FIXED_SHIFT 16
#define FIXED_ONE (1 << FIXED_SHIFT)

/* Blends two pixels by a weight of 0-256 for the second. */
static uint32_t lerp_pixel(uint32_t a, uint32_t b, uint32_t weight)
{
    uint32_t red_blue = ((a & 0x00FF00FF) * (256 - weight) + (b & 0x00FF00FF) * weight) >> 8;
    uint32_t alpha_green = (((a >> 8) & 0x00FF00FF) * (256 - weight) + ((b >> 8) & 0x00FF00FF) * weight);
    return (red_blue & 0x00FF00FF) | (alpha_green & 0xFF00FF00);
}

/*
 * The colour of the source at a fractional position: a weighted average of
 * the four pixels around it. This is what makes a scaled image look smooth
 * rather than blocky.
 */
static uint32_t sample_bilinear(const struct gfx_surface *source, struct gfx_rect bounds, int32_t fx, int32_t fy)
{
    int x0 = fx >> FIXED_SHIFT, y0 = fy >> FIXED_SHIFT;
    uint32_t weight_x = ((uint32_t)fx >> (FIXED_SHIFT - 8)) & 0xFF;
    uint32_t weight_y = ((uint32_t)fy >> (FIXED_SHIFT - 8)) & 0xFF;

    /* Stay inside the source rectangle at its right and bottom edges. */
    int x1 = x0 + 1 < bounds.x + bounds.width ? x0 + 1 : x0;
    int y1 = y0 + 1 < bounds.y + bounds.height ? y0 + 1 : y0;

    const uint32_t *row0 = source->pixels + (size_t)y0 * (size_t)source->stride;
    const uint32_t *row1 = source->pixels + (size_t)y1 * (size_t)source->stride;
    uint32_t top = lerp_pixel(row0[x0], row0[x1], weight_x);
    uint32_t bottom = lerp_pixel(row1[x0], row1[x1], weight_x);
    return lerp_pixel(top, bottom, weight_y);
}

void gfx_blend_scaled(struct gfx_surface *surface, struct gfx_rect target_rect, const struct gfx_surface *source,
                      struct gfx_rect source_rect, unsigned opacity)
{
    source_rect = gfx_rect_intersect(source_rect, gfx_surface_bounds(source));
    struct gfx_rect visible = gfx_rect_intersect(target_rect, surface->clip);
    if (opacity == 0 || gfx_rect_is_empty(visible) || gfx_rect_is_empty(source_rect)) {
        return;
    }
    /* Source pixels per target pixel. */
    int64_t step_x = ((int64_t)source_rect.width << FIXED_SHIFT) / target_rect.width;
    int64_t step_y = ((int64_t)source_rect.height << FIXED_SHIFT) / target_rect.height;
    int32_t limit_x = ((source_rect.x + source_rect.width) << FIXED_SHIFT) - 1;
    int32_t limit_y = ((source_rect.y + source_rect.height) << FIXED_SHIFT) - 1;

    for (int y = visible.y; y < visible.y + visible.height; y++) {
        /* Sample at pixel centres: half a step in, minus half a source pixel. */
        int64_t fy = ((int64_t)source_rect.y << FIXED_SHIFT) + (y - target_rect.y) * step_y + step_y / 2 - FIXED_ONE / 2;
        fy = fy < ((int64_t)source_rect.y << FIXED_SHIFT) ? ((int64_t)source_rect.y << FIXED_SHIFT) : fy;
        fy = fy > limit_y ? limit_y : fy;
        uint32_t *row = surface->pixels + (size_t)y * (size_t)surface->stride;

        for (int x = visible.x; x < visible.x + visible.width; x++) {
            int64_t fx = ((int64_t)source_rect.x << FIXED_SHIFT) + (x - target_rect.x) * step_x + step_x / 2 - FIXED_ONE / 2;
            fx = fx < ((int64_t)source_rect.x << FIXED_SHIFT) ? ((int64_t)source_rect.x << FIXED_SHIFT) : fx;
            fx = fx > limit_x ? limit_x : fx;

            uint32_t pixel = sample_bilinear(source, source_rect, (int32_t)fx, (int32_t)fy);
            if (opacity < 255) {
                pixel = scale_pixel(pixel, opacity);
            }
            row[x] = blend_over(row[x], pixel);
        }
    }
}
