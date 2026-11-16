/*
 * SSE2 versions of the row operations: four pixels per step.
 *
 * SSE2 has 128-bit registers and instructions that treat them as, for
 * example, sixteen bytes or eight 16-bit numbers, operating on all of them
 * at once. Blending needs 16 bits per channel for the multiplication, so
 * the four pixels are widened into two registers of two pixels each,
 * processed, and narrowed again.
 */
#include <emmintrin.h>

#include <gui/pixel.h>

#include "../../src/rowops.h"

/* x / 255, rounded, for sixteen-bit lanes holding 0..65025: the same
 * formula as div255() in <gui/pixel.h>, in every lane at once. */
static inline __m128i div255_epi16(__m128i x)
{
    x = _mm_add_epi16(x, _mm_set1_epi16(128));
    return _mm_srli_epi16(_mm_add_epi16(x, _mm_srli_epi16(x, 8)), 8);
}

/* For two widened pixels (8 lanes: B G R A B G R A), returns each pixel's
 * alpha copied into all four of its lanes. */
static inline __m128i spread_alpha(__m128i pixels)
{
    __m128i low = _mm_shufflelo_epi16(pixels, _MM_SHUFFLE(3, 3, 3, 3));
    return _mm_shufflehi_epi16(low, _MM_SHUFFLE(3, 3, 3, 3));
}

/* destination * (255 - source alpha) / 255 + source, for four pixels. */
static inline __m128i blend4(__m128i destination, __m128i source)
{
    const __m128i zero = _mm_setzero_si128();
    const __m128i full = _mm_set1_epi16(255);

    __m128i source_low = _mm_unpacklo_epi8(source, zero);
    __m128i source_high = _mm_unpackhi_epi8(source, zero);
    __m128i keep_low = _mm_sub_epi16(full, spread_alpha(source_low));
    __m128i keep_high = _mm_sub_epi16(full, spread_alpha(source_high));

    __m128i low = div255_epi16(_mm_mullo_epi16(_mm_unpacklo_epi8(destination, zero), keep_low));
    __m128i high = div255_epi16(_mm_mullo_epi16(_mm_unpackhi_epi8(destination, zero), keep_high));
    return _mm_add_epi8(_mm_packus_epi16(low, high), source);
}

/* All four channels of four pixels scaled by factor / 255. */
static inline __m128i scale4(__m128i pixels, __m128i factor)
{
    const __m128i zero = _mm_setzero_si128();
    __m128i low = div255_epi16(_mm_mullo_epi16(_mm_unpacklo_epi8(pixels, zero), factor));
    __m128i high = div255_epi16(_mm_mullo_epi16(_mm_unpackhi_epi8(pixels, zero), factor));
    return _mm_packus_epi16(low, high);
}

static void sse2_fill(uint32_t *row, int count, uint32_t pixel)
{
    uint32_t alpha = pixel >> 24;
    if (alpha == 0) {
        return;
    }
    __m128i source = _mm_set1_epi32((int)pixel);
    int x = 0;
    if (alpha == 255) {
        for (; x + 4 <= count; x += 4) {
            _mm_storeu_si128((__m128i *)(row + x), source);
        }
    } else {
        __m128i keep = _mm_set1_epi16((short)(255 - alpha));
        for (; x + 4 <= count; x += 4) {
            __m128i destination = _mm_loadu_si128((const __m128i *)(row + x));
            _mm_storeu_si128((__m128i *)(row + x), _mm_add_epi8(scale4(destination, keep), source));
        }
    }
    /* The last one to three pixels, one at a time. */
    for (; x < count; x++) {
        row[x] = blend_over(row[x], pixel);
    }
}

static void sse2_blend(uint32_t *row, const uint32_t *source, int count)
{
    int x = 0;
    for (; x + 4 <= count; x += 4) {
        __m128i destination = _mm_loadu_si128((const __m128i *)(row + x));
        __m128i pixels = _mm_loadu_si128((const __m128i *)(source + x));
        _mm_storeu_si128((__m128i *)(row + x), blend4(destination, pixels));
    }
    for (; x < count; x++) {
        row[x] = blend_over(row[x], source[x]);
    }
}

static void sse2_blend_opacity(uint32_t *row, const uint32_t *source, int count, uint32_t opacity)
{
    __m128i factor = _mm_set1_epi16((short)opacity);
    int x = 0;
    for (; x + 4 <= count; x += 4) {
        __m128i destination = _mm_loadu_si128((const __m128i *)(row + x));
        __m128i pixels = scale4(_mm_loadu_si128((const __m128i *)(source + x)), factor);
        _mm_storeu_si128((__m128i *)(row + x), blend4(destination, pixels));
    }
    for (; x < count; x++) {
        row[x] = blend_over(row[x], scale_pixel(source[x], opacity));
    }
}

static const struct rowops rowops_sse2 = {
    .name = "sse2",
    .fill = sse2_fill,
    .blend = sse2_blend,
    .blend_opacity = sse2_blend_opacity,
};

const struct rowops *const rowops_accelerated = &rowops_sse2;
