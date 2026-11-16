#ifndef CORE_PRINTF_H
#define CORE_PRINTF_H

#include <stdarg.h>
#include <stddef.h>

/*
 * printf-style formatting, shared by the kernel log and ksnprintf.
 *
 * Supported: %c %s %d %i %u %x %X %p %%
 *   flags:   '-' (left-align), '0' (zero-pad numbers)
 *   width:   decimal number, e.g. %8x
 *   length:  l, ll, z            e.g. %lu %llx %zu
 * Not supported: precision, '*', floating point (the kernel has no FPU use).
 * An unknown conversion is printed literally so the mistake is visible.
 */

/* Receives the formatted output one character at a time. */
typedef void (*format_emit_fn)(void *context, char character);

void kvformat(format_emit_fn emit, void *context, const char *format, va_list arguments);

/*
 * Formats into `buffer`, always NUL-terminating if size > 0. Returns the
 * length the full output has, which is >= size if it was truncated.
 */
int ksnprintf(char *buffer, size_t size, const char *format, ...) __attribute__((format(printf, 3, 4)));
int kvsnprintf(char *buffer, size_t size, const char *format, va_list arguments);

#endif
