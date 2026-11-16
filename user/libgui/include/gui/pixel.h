#ifndef GUI_PIXEL_H
#define GUI_PIXEL_H

#include <stdint.h>

/* Per-pixel arithmetic on premultiplied 32-bit pixels, for code that
 * composes images itself (the drawing library, the window server). */

/* x / 255, rounded, for x in 0..65025 (a product of two bytes). Exact, and
 * much cheaper than a division. */
static inline uint32_t div255(uint32_t x)
{
    return ((x + 128) * 257) >> 16;
}

/* Converts a straight-alpha colour to a premultiplied pixel, additionally
 * scaled by `coverage` (0-255), the fraction of the pixel the shape covers. */
static inline uint32_t premultiply(uint32_t color, uint32_t coverage)
{
    uint32_t alpha = div255((color >> 24) * coverage);
    uint32_t red = div255(((color >> 16) & 0xFF) * alpha);
    uint32_t green = div255(((color >> 8) & 0xFF) * alpha);
    uint32_t blue = div255((color & 0xFF) * alpha);
    return (alpha << 24) | (red << 16) | (green << 8) | blue;
}

/* Scales all four channels of a premultiplied pixel by factor / 255. */
static inline uint32_t scale_pixel(uint32_t pixel, uint32_t factor)
{
    /* Two channels at a time: red and blue sit in one word with a spare
     * byte between them, as do alpha and green. */
    uint32_t red_blue = (pixel & 0x00FF00FF) * factor + 0x00800080;
    uint32_t alpha_green = ((pixel >> 8) & 0x00FF00FF) * factor + 0x00800080;
    red_blue = ((red_blue + ((red_blue >> 8) & 0x00FF00FF)) >> 8) & 0x00FF00FF;
    alpha_green = (alpha_green + ((alpha_green >> 8) & 0x00FF00FF)) & 0xFF00FF00;
    return red_blue | alpha_green;
}

/* "Source over destination" for premultiplied pixels:
 *     result = source + destination * (1 - source alpha) */
static inline uint32_t blend_over(uint32_t destination, uint32_t source)
{
    uint32_t alpha = source >> 24;
    if (alpha == 255) {
        return source;
    }
    if (alpha == 0) {
        return destination;
    }
    return source + scale_pixel(destination, 255 - alpha);
}

#endif
