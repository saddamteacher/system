/*
 * Graphics test: draws a test pattern with text and a mouse cursor that
 * follows the mouse.
 *
 * It also talks to tools/qemu-gfx-check.py through the serial log: it
 * prints where things are, the script takes screenshots and verifies them,
 * and then presses 'q' to end the test.
 */
#include <display.h>
#include <errno.h>
#include <gui/gfx.h>
#include <ipc.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define TIMEOUT_MS 40000

/* Solid patches whose exact colours the check script verifies. */
#define PATCH_Y 48
#define PATCH_SIZE 64
static const struct {
    int x;
    gfx_color color;
} patches[] = {
    { 48, GFX_RGB(255, 0, 0) },
    { 128, GFX_RGB(0, 255, 0) },
    { 208, GFX_RGB(0, 0, 255) },
    { 288, GFX_RGB(255, 255, 255) },
    { 368, GFX_RGB(0, 0, 0) },
};

/* The cursor image: '#' is the black outline, '.' the white interior. The
 * hot spot (the point that "is" the cursor position) is the top-left tip. */
#define CURSOR_WIDTH 12
#define CURSOR_HEIGHT 19
static const char *const cursor_shape[CURSOR_HEIGHT] = {
    "#           ",
    "##          ",
    "#.#         ",
    "#..#        ",
    "#...#       ",
    "#....#      ",
    "#.....#     ",
    "#......#    ",
    "#.......#   ",
    "#........#  ",
    "#.........# ",
    "#......#####",
    "#...#..#    ",
    "#..# #..#   ",
    "#.#  #..#   ",
    "##    #..#  ",
    "#     #..#  ",
    "       ##   ",
    "            ",
};

static struct gfx_surface screen;  /* the display itself */
static struct gfx_surface pattern; /* the test pattern without the cursor */
static struct gfx_surface frame;   /* what is being composed for the screen */
static int cursor_x, cursor_y;

static struct gfx_font *load_font(const char *name)
{
    char path[64];
    snprintf(path, sizeof(path), "/fonts/%s.sdf", name);
    struct gfx_font *font = gfx_font_load(path);
    if (font == NULL) {
        printf("gfx: cannot load %s\n", path);
        exit(1);
    }
    return font;
}

static void draw_pattern(struct gfx_surface *surface)
{
    struct gfx_font *title = load_font("Inter-SemiBold-28");
    struct gfx_font *heading = load_font("Inter-SemiBold-15");
    struct gfx_font *body = load_font("Inter-Regular-13");
    struct gfx_font *medium = load_font("Inter-Medium-13");
    struct gfx_font *small = load_font("Inter-Regular-11");
    struct gfx_font *large = load_font("Inter-Regular-20");

    gfx_fill_gradient(surface, gfx_surface_bounds(surface), GFX_RGB(40, 52, 92), GFX_RGB(150, 96, 130));

    for (size_t i = 0; i < sizeof(patches) / sizeof(patches[0]); i++) {
        gfx_fill_rect(surface, gfx_rect_make(patches[i].x, PATCH_Y, PATCH_SIZE, PATCH_SIZE), patches[i].color);
    }

    /* A translucent panel with rounded corners; the gradient shows through. */
    struct gfx_rect panel = gfx_rect_make(48, 150, 560, 330);
    gfx_fill_rounded_rect(surface, panel, 14, GFX_RGBA(255, 255, 255, 215));
    gfx_draw_text(surface, title, panel.x + 24, panel.y + 48, "Saddev", -1, GFX_RGB(30, 30, 36));
    gfx_draw_text(surface, heading, panel.x + 24, panel.y + 82, "Graphics base: test pattern", -1,
                  GFX_RGB(60, 60, 70));
    gfx_draw_text(surface, body, panel.x + 24, panel.y + 116,
                  "The quick brown fox jumps over the lazy dog. 0123456789", -1, GFX_RGB(40, 40, 48));
    gfx_draw_text(surface, medium, panel.x + 24, panel.y + 140,
                  "Medium weight: THE QUICK BROWN FOX (){}[]<>!?@#$%&*", -1, GFX_RGB(40, 40, 48));
    gfx_draw_text(surface, body, panel.x + 24, panel.y + 164,
                  "Oʻzbekiston, gʻalaba — naïve café “quotes” …", -1,
                  GFX_RGB(40, 40, 48));
    gfx_draw_text(surface, small, panel.x + 24, panel.y + 186, "Small text at 11 pixels: anti-aliased from a glyph atlas",
                  -1, GFX_RGB(90, 90, 100));
    gfx_draw_text(surface, large, panel.x + 24, panel.y + 222, "Larger text at 20 pixels", -1, GFX_RGB(20, 80, 160));

    /* Buttons: rounded rectangles of several radii. */
    gfx_fill_rounded_rect(surface, gfx_rect_make(panel.x + 24, panel.y + 250, 110, 30), 6, GFX_RGB(0, 122, 255));
    gfx_draw_text(surface, medium, panel.x + 54, panel.y + 270, "Button", -1, GFX_WHITE);
    gfx_fill_rounded_rect(surface, gfx_rect_make(panel.x + 150, panel.y + 250, 110, 30), 15, GFX_RGB(52, 199, 89));
    gfx_draw_text(surface, medium, panel.x + 186, panel.y + 270, "Pill", -1, GFX_WHITE);
    gfx_stroke_rect(surface, gfx_rect_make(panel.x + 276, panel.y + 250, 110, 30), GFX_RGB(120, 120, 130));
    gfx_draw_text(surface, body, panel.x + 304, panel.y + 270, "Outline", -1, GFX_RGB(60, 60, 70));

    /* Circles, like window control buttons. */
    gfx_fill_circle(surface, panel.x + 430, panel.y + 265, 7, GFX_RGB(255, 95, 87));
    gfx_fill_circle(surface, panel.x + 452, panel.y + 265, 7, GFX_RGB(254, 188, 46));
    gfx_fill_circle(surface, panel.x + 474, panel.y + 265, 7, GFX_RGB(40, 200, 64));

    /* Overlapping translucent shapes: each must tint what is below it. */
    gfx_fill_circle(surface, 760, 250, 90, GFX_RGBA(255, 60, 60, 150));
    gfx_fill_circle(surface, 860, 250, 90, GFX_RGBA(60, 255, 60, 150));
    gfx_fill_circle(surface, 810, 340, 90, GFX_RGBA(60, 60, 255, 150));

    /* A surface with its own transparency, blended at several opacities. */
    struct gfx_surface badge;
    if (gfx_surface_create(&badge, 120, 60)) {
        gfx_fill_rounded_rect(&badge, gfx_surface_bounds(&badge), 16, GFX_RGB(255, 204, 0));
        gfx_draw_text(&badge, heading, 30, 37, "Blend", -1, GFX_RGB(40, 30, 0));
        gfx_blend(surface, 1000, 160, &badge, gfx_surface_bounds(&badge), 255);
        gfx_blend(surface, 1000, 240, &badge, gfx_surface_bounds(&badge), 160);
        gfx_blend(surface, 1000, 320, &badge, gfx_surface_bounds(&badge), 70);
        gfx_surface_destroy(&badge);
    }

    /* Clipping: only the part of this circle inside the box may appear. */
    struct gfx_rect box = gfx_rect_make(700, 520, 160, 110);
    gfx_stroke_rect(surface, gfx_rect_inset(box, 1), GFX_WHITE);
    gfx_set_clip(surface, box);
    gfx_fill_circle(surface, box.x + box.width, box.y + box.height, 120, GFX_RGB(255, 149, 0));
    gfx_reset_clip(surface);
    gfx_draw_text(surface, small, box.x, box.y + box.height + 18, "clipped to the box", -1, GFX_WHITE);

    gfx_draw_text(surface, body, 48, surface->height - 40, "Move the mouse. Press q to quit.", -1, GFX_WHITE);

    gfx_font_destroy(title);
    gfx_font_destroy(heading);
    gfx_font_destroy(body);
    gfx_font_destroy(medium);
    gfx_font_destroy(small);
    gfx_font_destroy(large);
}

static void draw_cursor(struct gfx_surface *surface, int x, int y)
{
    for (int row = 0; row < CURSOR_HEIGHT; row++) {
        for (int column = 0; column < CURSOR_WIDTH; column++) {
            char cell = cursor_shape[row][column];
            if (cell != ' ') {
                gfx_fill_rect(surface, gfx_rect_make(x + column, y + row, 1, 1), cell == '#' ? GFX_BLACK : GFX_WHITE);
            }
        }
    }
}

static struct gfx_rect cursor_rect(int x, int y)
{
    return gfx_rect_make(x, y, CURSOR_WIDTH, CURSOR_HEIGHT);
}

/* Rebuilds `area` of the frame from the pattern plus the cursor, then
 * copies just that area to the screen. Composing off-screen first means the
 * display never shows a half-drawn state. */
static void present(struct gfx_rect area)
{
    area = gfx_rect_intersect(area, gfx_surface_bounds(&frame));
    gfx_blit(&frame, area.x, area.y, &pattern, area);
    gfx_set_clip(&frame, area);
    draw_cursor(&frame, cursor_x, cursor_y);
    gfx_reset_clip(&frame);
    gfx_blit(&screen, area.x, area.y, &frame, area);
}

static int clamp(int value, int low, int high)
{
    return value < low ? low : (value > high ? high : value);
}

static void move_cursor(int dx, int dy)
{
    int old_x = cursor_x, old_y = cursor_y;
    cursor_x = clamp(cursor_x + dx, 0, screen.width - 1);
    cursor_y = clamp(cursor_y + dy, 0, screen.height - 1);
    /* Only the pixels under the old and the new cursor changed. */
    present(gfx_rect_union(cursor_rect(old_x, old_y), cursor_rect(cursor_x, cursor_y)));
}

int main(void)
{
    struct sd_fb_info info;
    if (display_info(&info) != 0) {
        printf("gfx: no display (errno %d)\n", errno);
        return 1;
    }
    uint32_t *memory = display_map();
    if (memory == NULL) {
        printf("gfx: cannot map the display (errno %d)\n", errno);
        return 1;
    }
    /* A second mapping must be refused while we hold the display. */
    if (display_map() != NULL || errno != EBUSY) {
        printf("gfx: the display could be mapped twice\n");
        return 1;
    }
    gfx_surface_init(&screen, memory, (int)info.width, (int)info.height, (int)(info.pitch / 4));
    if (!gfx_surface_create(&pattern, screen.width, screen.height)
        || !gfx_surface_create(&frame, screen.width, screen.height)) {
        printf("gfx: out of memory\n");
        return 1;
    }

    unsigned long long started = uptime_ms();
    draw_pattern(&pattern);
    unsigned long long drawn = uptime_ms();
    cursor_x = screen.width / 2;
    cursor_y = screen.height / 2;
    present(gfx_surface_bounds(&screen));
    unsigned long long presented = uptime_ms();

    printf("gfx: display %dx%d, pattern drawn in %llu ms, full frame presented in %llu ms\n", screen.width,
           screen.height, drawn - started, presented - drawn);
    for (size_t i = 0; i < sizeof(patches) / sizeof(patches[0]); i++) {
        printf("gfx: patch %d %d %06x\n", patches[i].x + PATCH_SIZE / 2, PATCH_Y + PATCH_SIZE / 2,
               (unsigned)(patches[i].color & 0xFFFFFF));
    }
    printf("gfx: cursor at %d %d\n", cursor_x, cursor_y);
    printf("gfx: ready\n");

    int mouse = open("/dev/mouse");
    int keyboard = open("/dev/keyboard");
    if (mouse < 0 || keyboard < 0) {
        printf("gfx: cannot open the input devices\n");
        return 1;
    }
    struct sd_pollfd inputs[2] = {
        { .fd = mouse, .events = SD_POLL_IN },
        { .fd = keyboard, .events = SD_POLL_IN },
    };
    unsigned long long deadline = uptime_ms() + TIMEOUT_MS;

    for (;;) {
        long long remaining = (long long)deadline - (long long)uptime_ms();
        if (remaining <= 0 || poll_fds(inputs, 2, remaining) <= 0) {
            printf("gfx: timed out waiting for input\n");
            return 1;
        }
        if (inputs[0].revents & SD_POLL_IN) {
            struct sd_mouse_event events[16];
            long size = read(mouse, events, sizeof(events));
            int dx = 0, dy = 0;
            for (long i = 0; i < size / (long)sizeof(events[0]); i++) {
                dx += events[i].dx;
                dy += events[i].dy;
            }
            move_cursor(dx, dy);
            printf("gfx: cursor at %d %d\n", cursor_x, cursor_y);
        }
        if (inputs[1].revents & SD_POLL_IN) {
            struct sd_key_event events[8];
            long size = read(keyboard, events, sizeof(events));
            for (long i = 0; i < size / (long)sizeof(events[0]); i++) {
                if (events[i].pressed && events[i].character == 'q') {
                    printf("gfx: quit requested\n");
                    return 0;
                }
            }
        }
    }
}
