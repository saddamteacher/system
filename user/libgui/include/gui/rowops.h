#ifndef GUI_ROWOPS_H
#define GUI_ROWOPS_H

#include <stdint.h>

/*
 * The innermost loops of compositing: blending a run of pixels within one
 * row. Each has a portable C version and, on x86_64, an SSE2 version that
 * handles four pixels per step.
 *
 * Which is faster depends on the machine (on real CPUs SSE2 wins clearly;
 * under an emulator it can lose), so the choice is made by timing both
 * once, the first time one is needed.
 */

/* Blends the premultiplied `pixel` over `count` pixels. */
void gfx_row_fill(uint32_t *row, int count, uint32_t pixel);

/* Blends `count` premultiplied pixels of `source` over `row`. */
void gfx_row_blend(uint32_t *row, const uint32_t *source, int count);

/* The same with every source pixel first scaled by opacity (0-255). */
void gfx_row_blend_opacity(uint32_t *row, const uint32_t *source, int count, uint32_t opacity);

/* "sse2" or "portable": which versions were selected, for diagnostics. */
const char *gfx_rowops_name(void);

/*
 * Runs every implementation on the same pseudo-random pixels and compares
 * the results with the portable one, bit for bit. Returns the number of
 * differing pixels (0 = all agree), or -1 if there is only one
 * implementation and so nothing to compare.
 */
int gfx_rowops_verify(void);

#endif
