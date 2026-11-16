#ifndef GUI_ANIM_H
#define GUI_ANIM_H

#include <stdbool.h>
#include <stdint.h>

/*
 * Animation building blocks.
 *
 * Everything here is driven by TIME, never by frame count: every function
 * takes the current time (nanoseconds, from clock_ns()) and computes where
 * the animation should be at that instant. An animation therefore takes
 * the same wall-clock time, and passes through the same positions, whether
 * the screen manages 60 frames per second or 20. A slow frame makes it
 * coarser, not slower.
 */

/* --------------------------------------------------------------- easing */

/*
 * An easing function reshapes progress: it maps linear time t in [0, 1] to
 * animation progress in [0, 1]. Linear motion looks mechanical; real
 * objects speed up and slow down.
 */
typedef float (*anim_easing_fn)(float t);

float anim_linear(float t);
/* Slow start, fast middle, slow end. The default for moving things. */
float anim_ease_in_out_cubic(float t);
/* Fast start, gentle stop. For things appearing in response to an action. */
float anim_ease_out_cubic(float t);
/* Gentle start, fast end. For things leaving. */
float anim_ease_in_cubic(float t);

/* --------------------------------------------------------------- tween */

/*
 * A tween moves one value from `from` to `to` over a fixed duration with
 * an easing function.
 */
struct anim_tween {
    float from, to;
    uint64_t start_ns;
    uint64_t duration_ns;
    anim_easing_fn easing;
};

void anim_tween_start(struct anim_tween *tween, float from, float to, uint32_t duration_ms, anim_easing_fn easing,
                      uint64_t now_ns);
/* The value at time `now_ns`; `from` before the start, `to` after the end. */
float anim_tween_value(const struct anim_tween *tween, uint64_t now_ns);
/* Progress from 0 to 1, after easing. */
float anim_tween_progress(const struct anim_tween *tween, uint64_t now_ns);
bool anim_tween_finished(const struct anim_tween *tween, uint64_t now_ns);

/* -------------------------------------------------------------- spring */

/*
 * A spring pulls a value towards a target like a weight on a spring with
 * friction. Unlike a tween it has no fixed duration and can be given a new
 * target at any moment: it simply continues from its current position and
 * speed, so interrupted motion stays smooth. Good for anything that
 * follows the user, like dock icons growing under the pointer.
 *
 *   stiffness  how hard it pulls: higher is faster
 *   damping    how much friction: lower overshoots and bounces; the value
 *              2 * sqrt(stiffness) settles as fast as possible without
 *              overshooting ("critically damped")
 */
struct anim_spring {
    float value;
    float velocity;
    float target;
    float stiffness;
    float damping;
    uint64_t last_ns; /* time the simulation has been advanced to; 0 = not yet */
};

void anim_spring_init(struct anim_spring *spring, float value, float stiffness, float damping);
void anim_spring_set_target(struct anim_spring *spring, float target);
/* Advances the simulation to `now_ns` and returns the value. */
float anim_spring_update(struct anim_spring *spring, uint64_t now_ns);
/* True once the value has (practically) reached the target and stopped. */
bool anim_spring_at_rest(const struct anim_spring *spring);

/* ---------------------------------------------------------------- misc */

/* Linear interpolation: `from` at t = 0, `to` at t = 1. */
static inline float anim_lerp(float from, float to, float t)
{
    return from + (to - from) * t;
}

#endif
