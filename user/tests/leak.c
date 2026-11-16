/*
 * Memory leak checks for user-space libraries.
 *
 * Each piece of work below is repeated, and after every repetition the
 * amount of allocated memory must be back where it started. A leak of even
 * one byte per round shows up.
 *
 * usage: leak          the checks that need no window server
 *        leak windows  window create/draw/destroy cycles (needs the server)
 */
#include <gui/gfx.h>
#include <gui/gui.h>
#include <gui/ui.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define ROUNDS 25

#define CHECK(condition) \
    do { \
        if (!(condition)) { \
            printf("leak: line %d: failed: %s\n", __LINE__, #condition); \
            return 1; \
        } \
    } while (0)

static int check_drawing(void)
{
    /* Warm up: the first use of some things allocates memory that is kept
     * on purpose (for example the allocator's own first arena). */
    struct gfx_font *font = gfx_font_load("/fonts/Inter-Regular-13.sdf");
    CHECK(font != NULL);
    gfx_font_destroy(font);
    size_t baseline = malloc_bytes_in_use();

    for (int round = 0; round < ROUNDS; round++) {
        struct gfx_surface canvas, picture, blurred;
        font = gfx_font_load("/fonts/Inter-Regular-13.sdf");
        CHECK(font != NULL);
        CHECK(gfx_surface_create(&canvas, 320, 200) && gfx_surface_create(&picture, 96, 96)
              && gfx_surface_create(&blurred, 320, 200));

        gfx_fill_gradient(&canvas, gfx_surface_bounds(&canvas), GFX_RGB(10, 20, 30), GFX_RGB(200, 100, 50));
        gfx_fill_rounded_rect(&picture, gfx_surface_bounds(&picture), 20, GFX_RGBA(255, 255, 255, 180));
        gfx_blend_scaled(&canvas, gfx_rect_make(20, 20, 150, 150), &picture, gfx_surface_bounds(&picture), 200);
        gfx_draw_text(&canvas, font, 10, 190, "leak check", -1, GFX_WHITE);
        gfx_box_blur(&canvas, gfx_surface_bounds(&canvas), 5, 2);
        CHECK(gfx_blur_backdrop(&blurred, &canvas, gfx_surface_bounds(&canvas)));

        gfx_surface_destroy(&blurred);
        gfx_surface_destroy(&picture);
        gfx_surface_destroy(&canvas);
        gfx_font_destroy(font);
        CHECK(malloc_bytes_in_use() == baseline);
    }
    return 0;
}

static int check_allocator(void)
{
    size_t baseline = malloc_bytes_in_use();
    for (int round = 0; round < ROUNDS; round++) {
        char *small = malloc(24);
        char *large = malloc(500000); /* an arena of its own */
        char *grown = realloc(malloc(100), 5000);
        char *zeroed = calloc(64, 64);
        CHECK(small != NULL && large != NULL && grown != NULL && zeroed != NULL);
        CHECK(malloc_bytes_in_use() > baseline);
        free(zeroed);
        free(grown);
        free(large);
        free(small);
        CHECK(malloc_bytes_in_use() == baseline);
    }
    return 0;
}

static int check_windows(void)
{
    struct gui_connection *connection = gui_connect("Leak Check");
    CHECK(connection != NULL);
    (void)ui_font(UI_FONT_BODY); /* loaded once and kept: part of the baseline */
    size_t baseline = malloc_bytes_in_use();

    for (int round = 0; round < ROUNDS; round++) {
        struct gui_window *window =
            gui_window_create(connection, GUI_WINDOW_NORMAL, 100 + round * 6, 120 + round * 4, 300, 180, "Leak", 0);
        CHECK(window != NULL);
        gfx_clear(&window->surface, gfx_surface_bounds(&window->surface), UI_COLOR_WINDOW);
        ui_text_centered(&window->surface, UI_FONT_BODY, gfx_surface_bounds(&window->surface), "leak check",
                         UI_COLOR_TEXT);
        gui_window_commit(window, gfx_surface_bounds(&window->surface), true);

        /* Wait until it is on screen, as a real program would. */
        struct gui_event event;
        int status;
        while ((status = gui_next_event(connection, &event, 2000)) > 0 && event.type != GUI_MSG_FRAME) {
        }
        CHECK(status > 0);
        /* Every third window is also resized before it goes. */
        if (round % 3 == 0) {
            CHECK(gui_window_resize(window, 340, 200));
            gfx_clear(&window->surface, gfx_surface_bounds(&window->surface), UI_COLOR_WINDOW);
            gui_window_commit(window, gfx_surface_bounds(&window->surface), false);
        }
        gui_window_destroy(window);
        CHECK(malloc_bytes_in_use() == baseline);
    }
    gui_disconnect(connection);
    printf("leak: %d windows created and destroyed without a leak\n", ROUNDS);
    return 0;
}

int main(int argc, char **argv)
{
    if (argc > 1 && strcmp(argv[1], "windows") == 0) {
        return check_windows();
    }
    if (check_allocator() != 0 || check_drawing() != 0) {
        return 1;
    }
    printf("leak: all checks passed\n");
    return 0;
}
