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
