#ifndef _STDIO_H
#define _STDIO_H

#include <stdarg.h>
#include <stddef.h>

#define EOF (-1)

/*
 * Output goes straight to file descriptor 1 with no buffering between
 * calls; each printf is one write.
 *
 * Supported conversions: %c %s %d %i %u %x %X %p %f %%
 *   flags '-' and '0', a width, a precision (".N" or ".*"),
 *   lengths l, ll, z.
 */
int printf(const char *format, ...) __attribute__((format(printf, 1, 2)));
int vprintf(const char *format, va_list arguments);
int snprintf(char *buffer, size_t size, const char *format, ...) __attribute__((format(printf, 3, 4)));
int vsnprintf(char *buffer, size_t size, const char *format, va_list arguments);
/* Like printf, to any file descriptor. */
int dprintf(int descriptor, const char *format, ...) __attribute__((format(printf, 2, 3)));

int puts(const char *text);
int putchar(int character);
/* Reads one character from descriptor 0; EOF at end of input. */
int getchar(void);

#endif
