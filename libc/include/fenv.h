#ifndef _FENV_H
#define _FENV_H

/*
 * Floating point environment: the rounding mode. The values are the x86
 * encodings of the mode as stored in the x87 control word.
 */
#define FE_TONEAREST  0x000
#define FE_DOWNWARD   0x400
#define FE_UPWARD     0x800
#define FE_TOWARDZERO 0xC00

/* Returns the current rounding mode. */
int fegetround(void);
/* Sets the rounding mode; returns 0, or non-zero for an invalid mode. */
int fesetround(int mode);

#endif
