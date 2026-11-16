/*
 * Straightforward byte-at-a-time implementations. This file is compiled with
 * flags that stop the compiler from recognising these loops as "a memset"
 * and replacing them with a call to memset, which would recurse forever
 * (see STRING_CFLAGS in the Makefile).
 */
#include <core/string.h>

void *memcpy(void *restrict destination, const void *restrict source, size_t count)
{
    unsigned char *out = destination;
    const unsigned char *in = source;
    for (size_t i = 0; i < count; i++) {
        out[i] = in[i];
    }
    return destination;
}

void *memmove(void *destination, const void *source, size_t count)
{
    unsigned char *out = destination;
    const unsigned char *in = source;

    /* When the regions overlap, copy in the direction that reads each source
     * byte before it is overwritten. */
    if (out < in) {
        for (size_t i = 0; i < count; i++) {
            out[i] = in[i];
        }
    } else {
        for (size_t i = count; i > 0; i--) {
            out[i - 1] = in[i - 1];
        }
    }
    return destination;
}

void *memset(void *destination, int value, size_t count)
{
    unsigned char *out = destination;
    for (size_t i = 0; i < count; i++) {
        out[i] = (unsigned char)value;
    }
    return destination;
}

int memcmp(const void *left, const void *right, size_t count)
{
    const unsigned char *a = left;
    const unsigned char *b = right;
    for (size_t i = 0; i < count; i++) {
        if (a[i] != b[i]) {
            return a[i] < b[i] ? -1 : 1;
        }
    }
    return 0;
}

size_t strlen(const char *text)
{
    size_t length = 0;
    while (text[length] != '\0') {
        length++;
    }
    return length;
}

int strcmp(const char *left, const char *right)
{
    while (*left != '\0' && *left == *right) {
        left++;
        right++;
    }
    return (unsigned char)*left - (unsigned char)*right;
}

int strncmp(const char *left, const char *right, size_t count)
{
    for (size_t i = 0; i < count; i++) {
        if (left[i] != right[i] || left[i] == '\0') {
            return (unsigned char)left[i] - (unsigned char)right[i];
        }
    }
    return 0;
}
