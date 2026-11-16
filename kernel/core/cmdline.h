#ifndef CORE_CMDLINE_H
#define CORE_CMDLINE_H

#include <stdbool.h>
#include <stddef.h>

/*
 * Looks up `key` in a command line of space-separated `key=value` words.
 * On success copies the value (truncated to fit, always NUL-terminated) into
 * `value` and returns true. `size` must be at least 1.
 */
bool cmdline_get(const char *cmdline, const char *key, char *value, size_t size);

#endif
