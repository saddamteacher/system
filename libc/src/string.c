/*
 * Compiled with -fno-builtin so the compiler does not turn these loops
 * into calls to the very functions they implement.
 */
#include <string.h>

#include <errno.h>
#include <stdint.h>

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

void *memchr(const void *memory, int value, size_t count)
{
    const unsigned char *bytes = memory;
    for (size_t i = 0; i < count; i++) {
        if (bytes[i] == (unsigned char)value) {
            return (void *)(uintptr_t)&bytes[i];
        }
    }
    return NULL;
}

size_t strlen(const char *text)
{
    size_t length = 0;
    while (text[length] != '\0') {
        length++;
    }
    return length;
}

size_t strnlen(const char *text, size_t limit)
{
    size_t length = 0;
    while (length < limit && text[length] != '\0') {
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

char *strcpy(char *restrict destination, const char *restrict source)
{
    size_t i = 0;
    do {
        destination[i] = source[i];
    } while (source[i++] != '\0');
    return destination;
}

size_t strlcpy(char *restrict destination, const char *restrict source, size_t count)
{
    size_t length = strlen(source);
    if (count > 0) {
        size_t copied = length < count - 1 ? length : count - 1;
        memcpy(destination, source, copied);
        destination[copied] = '\0';
    }
    return length;
}

size_t strlcat(char *restrict destination, const char *restrict source, size_t count)
{
    size_t used = strnlen(destination, count);
    if (used == count) {
        return count + strlen(source);
    }
    return used + strlcpy(destination + used, source, count - used);
}

char *strchr(const char *text, int character)
{
    for (;; text++) {
        if (*text == (char)character) {
            return (char *)(uintptr_t)text;
        }
        if (*text == '\0') {
            return NULL;
        }
    }
}

char *strrchr(const char *text, int character)
{
    const char *last = NULL;
    for (;; text++) {
        if (*text == (char)character) {
            last = text;
        }
        if (*text == '\0') {
            return (char *)(uintptr_t)last;
        }
    }
}

char *strstr(const char *text, const char *pattern)
{
    size_t length = strlen(pattern);
    for (; *text != '\0' || length == 0; text++) {
        if (strncmp(text, pattern, length) == 0) {
            return (char *)(uintptr_t)text;
        }
        if (*text == '\0') {
            break;
        }
    }
    return NULL;
}

const char *strerror(int error)
{
    switch (error) {
    case 0:            return "no error";
    case EPERM:        return "operation not permitted";
    case ENOENT:       return "no such file or directory";
    case ESRCH:        return "no such process";
    case EIO:          return "input/output error";
    case ENOEXEC:      return "not a valid executable";
    case EBADF:        return "bad file descriptor";
    case ECHILD:       return "no such child process";
    case EAGAIN:       return "try again";
    case ENOMEM:       return "out of memory";
    case EFAULT:       return "bad address";
    case EEXIST:       return "already exists";
    case ENOTDIR:      return "not a directory";
    case EISDIR:       return "is a directory";
    case EINVAL:       return "invalid argument";
    case EMFILE:       return "too many open files";
    case EROFS:        return "read-only file system";
    case EPIPE:        return "the other end is closed";
    case ENAMETOOLONG: return "name too long";
    case ENOSYS:       return "no such system call";
    case EMSGSIZE:     return "message too long";
    default:           return "unknown error";
    }
}
