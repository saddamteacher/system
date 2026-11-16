#ifndef SYSCALL_USER_MEMORY_H
#define SYSCALL_USER_MEMORY_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/*
 * Access to the calling process's memory from system calls.
 *
 * A pointer passed by a program is just a number the program chose. Before
 * the kernel reads or writes through it, the whole range must be checked to
 * lie in the program's own mapped memory. Otherwise a program could make
 * the kernel read kernel data for it, or overwrite the kernel.
 */

/* True if the calling process may read (or also write) the whole range. */
bool user_range_readable(uint64_t address, size_t length);
bool user_range_writable(uint64_t address, size_t length);

/* Checked copies. Return 0 or -EFAULT. */
int user_copy_in(void *destination, uint64_t user_source, size_t length);
int user_copy_out(uint64_t user_destination, const void *source, size_t length);

/*
 * Copies a NUL-terminated string of at most `capacity - 1` characters.
 * Returns its length, -EFAULT for a bad pointer or -ENAMETOOLONG.
 */
int user_copy_string(char *destination, size_t capacity, uint64_t user_source);

#endif
