#ifndef CORE_STRING_H
#define CORE_STRING_H

#include <stddef.h>

/*
 * The usual C memory and string functions. The compiler may emit calls to
 * memcpy/memset/memmove/memcmp by itself (struct copies, large initialisers),
 * so these four must exist with exactly these names and signatures.
 */
void *memcpy(void *restrict destination, const void *restrict source, size_t count);
void *memmove(void *destination, const void *source, size_t count);
void *memset(void *destination, int value, size_t count);
int memcmp(const void *left, const void *right, size_t count);

size_t strlen(const char *text);
int strcmp(const char *left, const char *right);
int strncmp(const char *left, const char *right, size_t count);

#endif
