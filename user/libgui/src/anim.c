#include <gui/anim.h>

#include <math.h>

#define NS_PER_MS 1000000ull
#define NS_PER_SECOND 1000000000.0f

/* --------------------------------------------------------------- easing */

float anim_linear(float t)
{
    return t;
}

float anim_ease_in_cubic(float t)
{
    return t * t * t;
}

float anim_ease_out_cubic(float t)
{
    float u = 1.0f - t;
    return 1.0f - u * u * u;
}

float anim_ease_in_out_cubic(float t)
{
    /* Two cubic halves joined in the middle: accelerate, then decelerate. */
    if (t < 0.5f) {
        return 4.0f * t * t * t;
    }
    float u = -2.0f * t + 2.0f;
    return 1.0f - u * u * u / 2.0f;
}

/* --------------------------------------------------------------- tween */

void anim_tween_start(struct anim_tween *tween, float from, float to, uint32_t duration_ms, anim_easing_fn easing,
                      uint64_t now_ns)
{
    tween->from = from;
    tween->to = to;
    tween->start_ns = now_ns;
    tween->duration_ns = (uint64_t)duration_ms * NS_PER_MS;
    tween->easing = easing;
}

bool anim_tween_finished(const struct anim_tween *tween, uint64_t now_ns)
{
    return now_ns >= tween->start_ns + tween->duration_ns;
}

float anim_tween_progress(const struct anim_tween *tween, uint64_t now_ns)
{
    if (now_ns <= tween->start_ns) {
        return 0.0f;
    }
    if (anim_tween_finished(tween, now_ns)) {
        return 1.0f;
    }
    float t = (float)(now_ns - tween->start_ns) / (float)tween->duration_ns;
    return tween->easing(t);
}

float anim_tween_value(const struct anim_tween *tween, uint64_t now_ns)
{
    return anim_lerp(tween->from, tween->to, anim_tween_progress(tween, now_ns));
}

/* -------------------------------------------------------------- spring */

/*
 * The simulation advances in fixed small steps, however much time passed
 * between two calls. Stepping by the frame time instead would make the
 * result depend on the frame rate, and a long frame could make the
 * numerical integration unstable (the spring would explode).
 */
#define SPRING_STEP_NS (2 * NS_PER_MS)
/* After a long pause (the program was not animating), do not simulate the
 * whole gap: nothing was moving, and it would cost thousands of steps. */
#define SPRING_MAX_CATCH_UP_NS (250 * NS_PER_MS)
#define SPRING_REST_DISTANCE 0.001f
#define SPRING_REST_VELOCITY 0.01f

void anim_spring_init(struct anim_spring *spring, float value, float stiffness, float damping)
{
    spring->value = value;
    spring->velocity = 0.0f;
    spring->target = value;
    spring->stiffness = stiffness;
    spring->damping = damping;
    spring->last_ns = 0;
}

void anim_spring_set_target(struct anim_spring *spring, float target)
{
    spring->target = target;
}

bool anim_spring_at_rest(const struct anim_spring *spring)
{
    return fabsf(spring->target - spring->value) < SPRING_REST_DISTANCE
        && fabsf(spring->velocity) < SPRING_REST_VELOCITY;
}

float anim_spring_update(struct anim_spring *spring, uint64_t now_ns)
{
    if (spring->last_ns == 0 || anim_spring_at_rest(spring)) {
        /* Nothing to simulate; restart the clock so that a later change of
         * target does not see a huge time gap. */
        if (anim_spring_at_rest(spring)) {
            spring->value = spring->target;
            spring->velocity = 0.0f;
        }
        spring->last_ns = now_ns;
        return spring->value;
    }
    if (now_ns - spring->last_ns > SPRING_MAX_CATCH_UP_NS) {
        spring->last_ns = now_ns - SPRING_MAX_CATCH_UP_NS;
    }

    float step = (float)SPRING_STEP_NS / NS_PER_SECOND;
    while (spring->last_ns + SPRING_STEP_NS <= now_ns) {
        /* Hooke's law plus friction: the pull grows with the distance to
         * the target, the friction with the speed. Velocity is updated
         * first and then used to move ("semi-implicit Euler"), which stays
         * stable where the naive order would not. */
        float acceleration =
            spring->stiffness * (spring->target - spring->value) - spring->damping * spring->velocity;
        spring->velocity += acceleration * step;
        spring->value += spring->velocity * step;
        spring->last_ns += SPRING_STEP_NS;
    }
    return spring->value;
}
