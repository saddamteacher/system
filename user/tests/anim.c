/*
 * The animation engine, tested with made-up clock values so the results
 * are exact and repeatable.
 */
#include <gui/anim.h>
#include <math.h>
#include <stdio.h>

#define MS 1000000ull

#define CHECK(condition) \
    do { \
        if (!(condition)) { \
            printf("anim: line %d: failed: %s\n", __LINE__, #condition); \
            return 1; \
        } \
    } while (0)

static int near(float a, float b, float tolerance)
{
    return fabsf(a - b) <= tolerance;
}

static int test_math(void)
{
    CHECK(near(sinf(0.0f), 0.0f, 1e-6f));
    CHECK(near(sinf(1.5707963f), 1.0f, 1e-5f));
    CHECK(near(sinf(3.1415927f), 0.0f, 1e-5f));
    CHECK(near(sinf(-1.0f), -0.84147098f, 1e-5f));
    CHECK(near(sinf(100.0f), -0.50636564f, 1e-4f)); /* far outside one period */
    CHECK(near(cosf(0.0f), 1.0f, 1e-5f));
    CHECK(near(cosf(2.0f), -0.41614684f, 1e-5f));
    return 0;
}

static int test_easing(void)
{
    anim_easing_fn curves[] = { anim_linear, anim_ease_in_cubic, anim_ease_out_cubic, anim_ease_in_out_cubic };
    for (unsigned c = 0; c < sizeof(curves) / sizeof(curves[0]); c++) {
        /* Every curve starts at 0, ends at 1 and never goes backwards. */
        CHECK(near(curves[c](0.0f), 0.0f, 1e-6f));
        CHECK(near(curves[c](1.0f), 1.0f, 1e-6f));
        float previous = 0.0f;
        for (int i = 1; i <= 100; i++) {
            float value = curves[c]((float)i / 100.0f);
            CHECK(value >= previous);
            previous = value;
        }
    }
    /* Ease-in-out is symmetric and slow at both ends. */
    CHECK(near(anim_ease_in_out_cubic(0.5f), 0.5f, 1e-6f));
    CHECK(anim_ease_in_out_cubic(0.1f) < 0.1f && anim_ease_in_out_cubic(0.9f) > 0.9f);
    CHECK(near(anim_ease_in_out_cubic(0.25f) + anim_ease_in_out_cubic(0.75f), 1.0f, 1e-6f));
    /* Ease-out is ahead of linear, ease-in behind. */
    CHECK(anim_ease_out_cubic(0.3f) > 0.3f && anim_ease_in_cubic(0.3f) < 0.3f);
    return 0;
}

static int test_tween(void)
{
    struct anim_tween tween;
    uint64_t start = 5000 * MS;
    anim_tween_start(&tween, 10.0f, 30.0f, 200, anim_linear, start);

    CHECK(near(anim_tween_value(&tween, start - 50 * MS), 10.0f, 1e-6f)); /* before the start */
    CHECK(near(anim_tween_value(&tween, start), 10.0f, 1e-6f));
    CHECK(near(anim_tween_value(&tween, start + 50 * MS), 15.0f, 1e-4f));
    CHECK(near(anim_tween_value(&tween, start + 100 * MS), 20.0f, 1e-4f));
    CHECK(!anim_tween_finished(&tween, start + 199 * MS));
    CHECK(anim_tween_finished(&tween, start + 200 * MS));
    CHECK(near(anim_tween_value(&tween, start + 900 * MS), 30.0f, 1e-6f)); /* stays at the end */

    /* The value depends on the time asked about, not on how often or in
     * what order it was asked: that is what "time-based" means. */
    float direct = anim_tween_value(&tween, start + 150 * MS);
    for (int i = 0; i < 37; i++) {
        (void)anim_tween_value(&tween, start + (uint64_t)i * 4 * MS);
    }
    CHECK(anim_tween_value(&tween, start + 150 * MS) == direct);
    return 0;
}

/* Runs a spring from 0 towards 1 for one second, sampled every
 * `frame_ms`, and returns where it is at 150 ms and at the end. */
static void run_spring(float damping, unsigned frame_ms, float *early, float *final, float *peak)
{
    struct anim_spring spring;
    anim_spring_init(&spring, 0.0f, 300.0f, damping);
    uint64_t now = 1000 * MS;
    anim_spring_update(&spring, now); /* starts the clock */
    anim_spring_set_target(&spring, 1.0f);

    *peak = 0.0f;
    *early = 0.0f;
    for (unsigned elapsed = frame_ms; elapsed <= 1000; elapsed += frame_ms) {
        float value = anim_spring_update(&spring, now + (uint64_t)elapsed * MS);
        if (value > *peak) {
            *peak = value;
        }
        if (elapsed <= 150) {
            *early = value;
        }
    }
    *final = spring.value;
}

static int test_spring(void)
{
    float early, final, peak;

    /* Critically damped (2 * sqrt(300) is about 34.6): reaches the target
     * without ever passing it. */
    run_spring(34.64f, 10, &early, &final, &peak);
    CHECK(near(final, 1.0f, 0.01f));
    CHECK(peak <= 1.001f);
    CHECK(early > 0.3f && early < 0.95f); /* on its way, not there yet */

    /* Lightly damped: overshoots, then still settles on the target. */
    run_spring(10.0f, 10, &early, &final, &peak);
    CHECK(peak > 1.1f);
    CHECK(near(final, 1.0f, 0.05f));

    /* Frame-rate independence: the same spring sampled at 100 and at 20
     * frames per second is at the same place at the same time. */
    float early_fast, final_fast, early_slow, final_slow;
    run_spring(20.0f, 10, &early_fast, &final_fast, &peak);
    run_spring(20.0f, 50, &early_slow, &final_slow, &peak);
    CHECK(near(early_fast, early_slow, 1e-4f));
    CHECK(near(final_fast, final_slow, 1e-4f));

    /* A spring at rest stays exactly on its target. */
    struct anim_spring still;
    anim_spring_init(&still, 5.0f, 300.0f, 30.0f);
    CHECK(anim_spring_at_rest(&still));
    CHECK(anim_spring_update(&still, 1 * MS) == 5.0f && anim_spring_update(&still, 99999 * MS) == 5.0f);
    return 0;
}

int main(void)
{
    if (test_math() != 0 || test_easing() != 0 || test_tween() != 0 || test_spring() != 0) {
        return 1;
    }
    printf("anim: all checks passed\n");
    return 0;
}
