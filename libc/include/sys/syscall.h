#ifndef _SYS_SYSCALL_H
#define _SYS_SYSCALL_H

#include <saddev/syscall.h>

/* Performs a system call with up to six arguments and returns the raw
 * result: a value, or a negative error code. Implemented in assembly. */
long syscall6(long number, long a0, long a1, long a2, long a3, long a4, long a5);

#endif
