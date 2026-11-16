#ifndef _STRING_H
#define _STRING_H

#include <stddef.h>

void *memcpy(void *restrict destination, const void *restrict source, size_t count);
void *memmove(void *destination, const void *source, size_t count);
void *memset(void *destination, int value, size_t count);
int memcmp(const void *left, const void *right, size_t count);
void *memchr(const void *memory, int value, size_t count);

size_t strlen(const char *text);
size_t strnlen(const char *text, size_t limit);
int strcmp(const char *left, const char *right);
int strncmp(const char *left, const char *right, size_t count);
char *strcpy(char *restrict destination, const char *restrict source);
/* Copies at most count - 1 characters and always terminates the result.
 * Returns the length of `source` (so truncation is detectable). */
size_t strlcpy(char *restrict destination, const char *restrict source, size_t count);
size_t strlcat(char *restrict destination, const char *restrict source, size_t count);
char *strchr(const char *text, int character);
char *strrchr(const char *text, int character);
char *strstr(const char *text, const char *pattern);

/* Text for an error code from <errno.h>. */
const char *strerror(int error);

#endif
