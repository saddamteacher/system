/*
 * Window animations: opening, closing, minimizing into the dock and
 * restoring from it.
 *
 * An animated window is not drawn from its live buffer. When an animation
 * starts, the server paints the window (title bar and content) once into
 * an image, and for the duration draws that image scaled, moved and faded.
 * The image is called a "ghost" here: it can outlive its window, which is
 * what lets a closed window fade away after its application is gone.
 */
#include "compositor.h"

#include <gui/anim.h>
#include <unistd.h>

#define MAX_GHOSTS 8

#define OPEN_MS 220
#define CLOSE_MS 180
#define MINIMIZE_MS 340
#define OPEN_START_SCALE 0.90f
#define CLOSE_END_SCALE 0.88f
#define MINIMIZED_OPACITY 0.35f

struct ghost {
    bool used;
    const char *kind; /* for the log */
    uint32_t window_id;
    struct gfx_surface image;
    struct gfx_rect from, to;
    float opacity_from, opacity_to;
    struct anim_tween tween;
    /* The window to show again when the animation ends (open, restore). */
    struct window *reveal;
    /* Where and how the ghost is drawn in the current frame. */
    struct gfx_rect current;
    unsigned opacity;
    unsigned frames;
};

static struct ghost ghosts[MAX_GHOSTS];
/* Stretches every duration; the check uses it to look at animations
 * half-way through. */
static unsigned duration_scale = 1;

void animations_set_scale(unsigned scale)
{
    duration_scale = scale > 0 ? scale : 1;
}

bool animations_active(void)
{
    for (int i = 0; i < MAX_GHOSTS; i++) {
        if (ghosts[i].used) {
            return true;
        }
    }
    return false;
}

/* Paints a window, without its shadow, into a new image the size of its
 * frame. */
static bool snapshot(const struct window *window, struct gfx_surface *image)
{
    struct gfx_rect frame = window_frame_rect(window);
    if (!gfx_surface_create(image, frame.width, frame.height)) {
        return false;
    }
    /* Draw a copy of the window positioned so its frame starts at (0, 0). */
    struct window local = *window;
    local.x = 0;
    local.y = frame.height - window->height;
    decor_draw(image, &local, window == focused_window, false);
    decor_draw_content(image, &local);
    return true;
}

static struct gfx_rect scale_about_center(struct gfx_rect rect, float scale)
{
    int width = (int)((float)rect.width * scale + 0.5f);
    int height = (int)((float)rect.height * scale + 0.5f);
    return gfx_rect_make(rect.x + (rect.width - width) / 2, rect.y + (rect.height - height) / 2, width, height);
}

static void ghost_finish(struct ghost *ghost, bool log)
{
    damage_add_overlay(ghost->current);
    if (ghost->reveal != NULL) {
        ghost->reveal->animation_hidden = false;
        damage_add(window_visual_rect(ghost->reveal));
    }
    if (log) {
        uint64_t took_ms = (clock_ns() - ghost->tween.start_ns) / 1000000ull;
        compositor_log("animation %s window %u took %llu ms in %u frames", ghost->kind, ghost->window_id,
                       (unsigned long long)took_ms, ghost->frames);
    }
    gfx_surface_destroy(&ghost->image);
    ghost->used = false;
}

/* A window gets at most one animation: a new one replaces the old. */
static void cancel_for(const struct window *window)
{
    for (int i = 0; i < MAX_GHOSTS; i++) {
        if (ghosts[i].used && ghosts[i].window_id == window->id) {
            ghost_finish(&ghosts[i], false);
        }
    }
}

static struct ghost *ghost_start(const char *kind, struct window *window, struct gfx_rect from, struct gfx_rect to,
                                 float opacity_from, float opacity_to, uint32_t duration_ms, anim_easing_fn easing)
{
    cancel_for(window);
    struct ghost *ghost = NULL;
    for (int i = 0; i < MAX_GHOSTS && ghost == NULL; i++) {
        if (!ghosts[i].used) {
            ghost = &ghosts[i];
        }
    }
    /* No free slot or no memory: the change simply happens at once. */
    if (ghost == NULL || !snapshot(window, &ghost->image)) {
        return NULL;
    }
    ghost->used = true;
    ghost->kind = kind;
    ghost->window_id = window->id;
    ghost->from = from;
    ghost->to = to;
    ghost->opacity_from = opacity_from;
    ghost->opacity_to = opacity_to;
    ghost->reveal = NULL;
    ghost->current = from;
    ghost->opacity = (unsigned)(opacity_from * 255.0f);
    ghost->frames = 0;
    anim_tween_start(&ghost->tween, 0.0f, 1.0f, duration_ms * duration_scale, easing, clock_ns());
    damage_add_overlay(from);
    return ghost;
}

/* Where a window goes when minimized: its dock icon if the shell told us,
 * otherwise the bottom centre of the screen. */
static struct gfx_rect minimize_target(const struct window *window)
{
    if (window->has_icon_rect) {
        return window->icon_rect;
    }
    return gfx_rect_make(screen.width / 2 - 24, screen.height - 60, 48, 48);
}

void animate_open(struct window *window)
{
    struct gfx_rect frame = window_frame_rect(window);
    struct ghost *ghost = ghost_start("open", window, scale_about_center(frame, OPEN_START_SCALE), frame, 0.0f, 1.0f,
                                      OPEN_MS, anim_ease_out_cubic);
    if (ghost != NULL) {
        window->animation_hidden = true;
        ghost->reveal = window;
    }
}

void animate_close(struct window *window)
{
    struct gfx_rect frame = window_frame_rect(window);
    ghost_start("close", window, frame, scale_about_center(frame, CLOSE_END_SCALE), 1.0f, 0.0f, CLOSE_MS,
                anim_ease_in_cubic);
}

void animate_minimize(struct window *window)
{
    ghost_start("minimize", window, window_frame_rect(window), minimize_target(window), 1.0f, MINIMIZED_OPACITY,
                MINIMIZE_MS, anim_ease_in_out_cubic);
}

void animate_restore(struct window *window)
{
    struct ghost *ghost = ghost_start("restore", window, minimize_target(window), window_frame_rect(window),
                                      MINIMIZED_OPACITY, 1.0f, MINIMIZE_MS, anim_ease_in_out_cubic);
    if (ghost != NULL) {
        window->animation_hidden = true;
        ghost->reveal = window;
    }
}

void animations_window_gone(struct window *window)
{
    for (int i = 0; i < MAX_GHOSTS; i++) {
        if (ghosts[i].used && ghosts[i].reveal == window) {
            /* It was appearing and is now gone: drop the animation. A
             * closing ghost has no `reveal` and is unaffected. */
            ghosts[i].reveal = NULL;
            ghost_finish(&ghosts[i], false);
        }
    }
}

static int lerp_int(int from, int to, float t)
{
    return (int)(anim_lerp((float)from, (float)to, t) + 0.5f);
}

void animations_update(uint64_t now_ns)
{
    for (int i = 0; i < MAX_GHOSTS; i++) {
        struct ghost *ghost = &ghosts[i];
        if (!ghost->used) {
            continue;
        }
        if (anim_tween_finished(&ghost->tween, now_ns)) {
            ghost_finish(ghost, true);
            continue;
        }
        /* Everything about the frame follows from the time alone. */
        float t = anim_tween_progress(&ghost->tween, now_ns);
        damage_add_overlay(ghost->current);
        ghost->current = gfx_rect_make(lerp_int(ghost->from.x, ghost->to.x, t), lerp_int(ghost->from.y, ghost->to.y, t),
                                       lerp_int(ghost->from.width, ghost->to.width, t),
                                       lerp_int(ghost->from.height, ghost->to.height, t));
        ghost->opacity = (unsigned)(anim_lerp(ghost->opacity_from, ghost->opacity_to, t) * 255.0f + 0.5f);
        ghost->frames++;
        damage_add_overlay(ghost->current);
    }
}

void animations_draw(struct gfx_surface *target)
{
    for (int i = 0; i < MAX_GHOSTS; i++) {
        const struct ghost *ghost = &ghosts[i];
        if (ghost->used && ghost->current.width > 0 && ghost->current.height > 0) {
            gfx_blend_scaled(target, ghost->current, &ghost->image, gfx_surface_bounds(&ghost->image), ghost->opacity);
        }
    }
}
