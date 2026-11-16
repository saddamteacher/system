#include <gui/rowops.h>

#include <gui/pixel.h>
#include <stddef.h>
#include <unistd.h>

#include "rowops.h"

/* ------------------------------------------------------------- portable */

static void portable_fill(uint32_t *row, int count, uint32_t pixel)
{
    uint32_t alpha = pixel >> 24;
    if (alpha == 255) {
        for (int x = 0; x < count; x++) {
            row[x] = pixel;
        }
    } else if (alpha != 0) {
        uint32_t keep = 255 - alpha;
        for (int x = 0; x < count; x++) {
            row[x] = pixel + scale_pixel(row[x], keep);
        }
    }
}

static void portable_blend(uint32_t *row, const uint32_t *source, int count)
{
    for (int x = 0; x < count; x++) {
        row[x] = blend_over(row[x], source[x]);
    }
}

static void portable_blend_opacity(uint32_t *row, const uint32_t *source, int count, uint32_t opacity)
{
    for (int x = 0; x < count; x++) {
        row[x] = blend_over(row[x], scale_pixel(source[x], opacity));
    }
}

const struct rowops rowops_portable = {
    .name = "portable",
    .fill = portable_fill,
    .blend = portable_blend,
    .blend_opacity = portable_blend_opacity,
};

/* ------------------------------------------------------------ selection */

#define TRIAL_PIXELS 2048
#define TRIAL_ROUNDS 24

static const struct rowops *selected;

/* Time for a fixed amount of typical work with one implementation. */
static uint64_t time_trial(const struct rowops *ops)
{
    static uint32_t row[TRIAL_PIXELS], source[TRIAL_PIXELS];
    for (int i = 0; i < TRIAL_PIXELS; i++) {
        row[i] = 0xFF204060u;
        source[i] = 0x80402010u; /* half transparent: the expensive case */
    }
    uint64_t start = clock_ns();
    for (int round = 0; round < TRIAL_ROUNDS; round++) {
        ops->blend(row, source, TRIAL_PIXELS);
        ops->fill(row, TRIAL_PIXELS, 0x80101010u);
    }
    return clock_ns() - start;
}

static const struct rowops *select_fastest(void)
{
    if (rowops_accelerated == NULL) {
        return &rowops_portable;
    }
    /* Best of three for each: one run can be disturbed by an interrupt. */
    uint64_t portable = UINT64_MAX, accelerated = UINT64_MAX;
    for (int attempt = 0; attempt < 3; attempt++) {
        uint64_t time = time_trial(&rowops_portable);
        portable = time < portable ? time : portable;
        time = time_trial(rowops_accelerated);
        accelerated = time < accelerated ? time : accelerated;
    }
    return accelerated < portable ? rowops_accelerated : &rowops_portable;
}

static const struct rowops *ops(void)
{
    if (selected == NULL) {
        selected = select_fastest();
    }
    return selected;
}

void gfx_row_fill(uint32_t *row, int count, uint32_t pixel)
{
    ops()->fill(row, count, pixel);
}

void gfx_row_blend(uint32_t *row, const uint32_t *source, int count)
{
    ops()->blend(row, source, count);
}

void gfx_row_blend_opacity(uint32_t *row, const uint32_t *source, int count, uint32_t opacity)
{
    ops()->blend_opacity(row, source, count, opacity);
}

const char *gfx_rowops_name(void)
{
    return ops()->name;
}

/* --------------------------------------------------------- verification */

#define VERIFY_PIXELS 67 /* not a multiple of four: exercises the tail loops */
#define VERIFY_ROUNDS 400

static uint32_t verify_random(uint32_t *state)
{
    *state = *state * 1664525u + 1013904223u;
    return *state;
}

/* A valid premultiplied pixel: no colour channel exceeds the alpha. */
static uint32_t random_pixel(uint32_t *state)
{
    uint32_t bits = verify_random(state);
    uint32_t alpha = bits >> 24;
    /* Make fully transparent and fully opaque pixels common. */
    if ((bits & 7) == 0) {
        alpha = 0;
    } else if ((bits & 7) == 1) {
        alpha = 255;
    }
    uint32_t red = div255(((bits >> 16) & 0xFF) * alpha);
    uint32_t green = div255(((bits >> 8) & 0xFF) * alpha);
    uint32_t blue = div255(((verify_random(state) >> 13) & 0xFF) * alpha);
    return (alpha << 24) | (red << 16) | (green << 8) | blue;
}

int gfx_rowops_verify(void)
{
    if (rowops_accelerated == NULL) {
        return -1;
    }
    uint32_t state = 20260101;
    int mismatches = 0;

    for (int round = 0; round < VERIFY_ROUNDS; round++) {
        uint32_t destination[VERIFY_PIXELS], source[VERIFY_PIXELS];
        uint32_t expected[VERIFY_PIXELS], actual[VERIFY_PIXELS];
        for (int i = 0; i < VERIFY_PIXELS; i++) {
            destination[i] = random_pixel(&state) | 0xFF000000u; /* an opaque screen */
            source[i] = random_pixel(&state);
        }
        uint32_t pixel = random_pixel(&state);
        uint32_t opacity = verify_random(&state) >> 24;

        for (int operation = 0; operation < 3; operation++) {
            for (int i = 0; i < VERIFY_PIXELS; i++) {
                expected[i] = actual[i] = destination[i];
            }
            if (operation == 0) {
                rowops_portable.fill(expected, VERIFY_PIXELS, pixel);
                rowops_accelerated->fill(actual, VERIFY_PIXELS, pixel);
            } else if (operation == 1) {
                rowops_portable.blend(expected, source, VERIFY_PIXELS);
                rowops_accelerated->blend(actual, source, VERIFY_PIXELS);
            } else {
                rowops_portable.blend_opacity(expected, source, VERIFY_PIXELS, opacity);
                rowops_accelerated->blend_opacity(actual, source, VERIFY_PIXELS, opacity);
            }
            for (int i = 0; i < VERIFY_PIXELS; i++) {
                mismatches += expected[i] != actual[i];
            }
        }
    }
    return mismatches;
}
