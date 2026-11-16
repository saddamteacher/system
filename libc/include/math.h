#ifndef _MATH_H
#define _MATH_H

/* The small part of <math.h> the system needs so far. */

float sqrtf(float x);
double sqrt(double x);
float fabsf(float x);
double fabs(double x);
float floorf(float x);
float ceilf(float x);
/* Rounds to the nearest integer, halfway cases away from zero. */
float roundf(float x);
long lroundf(float x);
float fminf(float a, float b);
/* Angles in radians. Accurate to about 1e-6. */
float sinf(float x);
float cosf(float x);

#define M_PI 3.14159265358979323846
float fmaxf(float a, float b);

#endif
