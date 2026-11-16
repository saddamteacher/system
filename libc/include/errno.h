#ifndef _ERRNO_H
#define _ERRNO_H

#include <saddev/errno.h>

/* Set by library functions that fail. One per process: programs have a
 * single thread. */
extern int errno;

#endif
