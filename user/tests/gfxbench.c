/*
 * Measures how long the drawing operations the window server depends on
 * take, on a screen-sized surface. Prints one line per operation.
 *
 * Run it from the terminal ("/test/gfxbench") or with `make bench`.
 */
#include <gui/gfx.h>
#include <gui/rowops.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define WIDTH 1280
#define HEIGHT 800
#define PIXELS (WIDTH * HEIGHT)

static struct gfx_surface target, source, translucent;
static struct gfx_font *font;

typedef void (*operation_fn)(void);

static void op_memset(void)
{
    memset(target.pixels, 0x55, (size_t)PIXELS * 4);
}

static void op_memcpy(void)
{
    memcpy(target.pixels, source.pixels, (size_t)PIXELS * 4);
}

static void op_clear(void)
{
    gfx_clear(&target, gfx_surface_bounds(&target), GFX_RGB(40, 60, 90));
}

static void op_fill_translucent(void)
{
    gfx_fill_rect(&target, gfx_surface_bounds(&target), GFX_RGBA(255, 255, 255, 128));
}

static void op_blit(void)
{
    gfx_blit(&target, 0, 0, &source, gfx_surface_bounds(&source));
}

static void op_blend_opaque(void)
{
    gfx_blend(&target, 0, 0, &source, gfx_surface_bounds(&source), 255);
}

static void op_blend_translucent(void)
{
    gfx_blend(&target, 0, 0, &translucent, gfx_surface_bounds(&translucent), 255);
}

static void op_blend_scaled(void)
{
    gfx_blend_scaled(&target, gfx_rect_make(0, 0, WIDTH, HEIGHT), &source, gfx_rect_make(0, 0, 640, 400), 255);
}

static void op_blur(void)
{
    gfx_box_blur(&target, gfx_surface_bounds(&target), 9, 1);
}

static struct gfx_surface blurred;

static void op_backdrop_blur(void)
{
    gfx_blur_backdrop(&blurred, &source, gfx_surface_bounds(&source));
}

static void op_gradient(void)
{
    gfx_fill_gradient(&target, gfx_surface_bounds(&target), GFX_RGB(20, 30, 80), GFX_RGB(200, 120, 90));
}

static void op_rounded_rect(void)
{
    gfx_fill_rounded_rect(&target, gfx_rect_make(40, 40, WIDTH - 80, HEIGHT - 80), 40, GFX_RGB(240, 240, 245));
}

#define TEXT_LINES 40
static void op_text(void)
{
    for (int line = 0; line < TEXT_LINES; line++) {
        gfx_draw_text(&target, font, 10, 20 + line * 18,
                      "The quick brown fox jumps over the lazy dog. 0123456789 The quick brown fox jumps.", -1,
                      GFX_WHITE);
    }
}

/* Runs an operation repeatedly for at least 60 ms and reports the average. */
static void measure(const char *name, operation_fn operation, unsigned long units, const char *unit_name)
{
    unsigned runs = 0;
    unsigned long long start = clock_ns(), elapsed;
    do {
        operation();
        runs++;
        elapsed = clock_ns() - start;
    } while (elapsed < 60000000ull && runs < 200);

    unsigned long long per_run_us = elapsed / runs / 1000;
    unsigned long long per_unit_ps = elapsed * 1000 / runs / units; /* picoseconds */
    printf("gfxbench: %-22s %6llu us  %4llu.%03llu ns per %s\n", name, per_run_us, per_unit_ps / 1000,
           per_unit_ps % 1000, unit_name);
}

int main(void)
{
    font = gfx_font_load("/fonts/Inter-Regular-13.sdf");
    if (!gfx_surface_create(&target, WIDTH, HEIGHT) || !gfx_surface_create(&source, WIDTH, HEIGHT)
        || !gfx_surface_create(&translucent, WIDTH, HEIGHT) || !gfx_surface_create(&blurred, WIDTH, HEIGHT)
        || font == NULL) {
        printf("gfxbench: cannot set up\n");
        return 1;
    }
    gfx_fill_gradient(&source, gfx_surface_bounds(&source), GFX_RGB(255, 200, 100), GFX_RGB(40, 90, 200));
    gfx_clear(&translucent, gfx_surface_bounds(&translucent), GFX_RGBA(120, 200, 255, 140));

    printf("gfxbench: %dx%d, one full-screen operation each; row operations: %s\n", WIDTH, HEIGHT,
           gfx_rowops_name());
    measure("memset", op_memset, PIXELS, "pixel");
    measure("memcpy", op_memcpy, PIXELS, "pixel");
    measure("clear (opaque fill)", op_clear, PIXELS, "pixel");
    measure("fill, 50% alpha", op_fill_translucent, PIXELS, "pixel");
    measure("blit (copy)", op_blit, PIXELS, "pixel");
    measure("blend, opaque source", op_blend_opaque, PIXELS, "pixel");
    measure("blend, translucent", op_blend_translucent, PIXELS, "pixel");
    measure("blend scaled 2x", op_blend_scaled, PIXELS, "pixel");
    measure("box blur, 1 pass", op_blur, PIXELS, "pixel");
    measure("backdrop blur", op_backdrop_blur, PIXELS, "pixel");
    measure("gradient", op_gradient, PIXELS, "pixel");
    measure("rounded rectangle", op_rounded_rect, PIXELS, "pixel");
    measure("text, 40 lines", op_text, TEXT_LINES * 82, "glyph");
    /* Whichever row operations were selected for speed, all of them must
     * compute exactly the same pixels. */
    int mismatches = gfx_rowops_verify();
    printf("gfxbench: accelerated and portable row operations differ in %d pixels\n", mismatches);
    if (mismatches > 0) {
        return 1;
    }
    printf("gfxbench: done\n");
    return 0;
}
