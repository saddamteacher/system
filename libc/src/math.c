#include <math.h>

/* Built with -fno-math-errno, so these builtins compile to the CPU's
 * square root instruction instead of calling back into this function. */
float sqrtf(float x)
{
    return __builtin_sqrtf(x);
}

double sqrt(double x)
{
    return __builtin_sqrt(x);
}

float fabsf(float x)
{
    return x < 0 ? -x : x;
}

double fabs(double x)
{
    return x < 0 ? -x : x;
}

/* Beyond this magnitude a float has no fractional bits left, and the value
 * would not fit the integer conversion below. */
#define FLOAT_ALL_INTEGERS 8388608.0f /* 2^23 */

float floorf(float x)
{
    if (!(fabsf(x) < FLOAT_ALL_INTEGERS)) {
        return x; /* already an integer, or infinite, or NaN */
    }
    /* The conversion truncates towards zero; correct it for negatives. */
    float truncated = (float)(long)x;
    return truncated > x ? truncated - 1.0f : truncated;
}

float ceilf(float x)
{
    return -floorf(-x);
}

float roundf(float x)
{
    return x < 0 ? -floorf(-x + 0.5f) : floorf(x + 0.5f);
}

long lroundf(float x)
{
    return (long)roundf(x);
}

float fminf(float a, float b)
{
    return a < b ? a : b;
}

float fmaxf(float a, float b)
{
    return a > b ? a : b;
}

#define PI_F 3.14159265358979f
#define HALF_PI_F 1.57079632679490f
#define TWO_PI_F 6.28318530717959f

float sinf(float x)
{
    /* Bring x into [-pi, pi]: the sine repeats every 2 pi. */
    x -= TWO_PI_F * floorf((x + PI_F) / TWO_PI_F);
    /* Fold into [-pi/2, pi/2], where the series below converges fast:
     * sin(pi - x) = sin(x). */
    if (x > HALF_PI_F) {
        x = PI_F - x;
    } else if (x < -HALF_PI_F) {
        x = -PI_F - x;
    }
    /* Taylor series x - x^3/3! + x^5/5! - x^7/7! + x^9/9! - x^11/11!,
     * evaluated from the inside out (Horner's scheme). */
    float x2 = x * x;
    return x * (1.0f + x2 * (-1.0f / 6.0f + x2 * (1.0f / 120.0f + x2 * (-1.0f / 5040.0f
                + x2 * (1.0f / 362880.0f + x2 * (-1.0f / 39916800.0f))))));
}

float cosf(float x)
{
    return sinf(x + HALF_PI_F);
}
