#ifndef _STDLIB_H
#define _STDLIB_H

#include <stddef.h>

#define EXIT_SUCCESS 0
#define EXIT_FAILURE 1

_Noreturn void exit(int status);
_Noreturn void abort(void);

void *malloc(size_t size);
void *calloc(size_t count, size_t size);
void *realloc(void *pointer, size_t size);
void free(void *pointer);

/* Bytes currently allocated with malloc and not yet freed (including the
 * allocator's per-block bookkeeping). For finding memory leaks: the value
 * before and after a piece of work that cleans up after itself is equal. */
size_t malloc_bytes_in_use(void);

int atoi(const char *text);
long strtol(const char *text, char **end, int base);
int abs(int value);

#endif
