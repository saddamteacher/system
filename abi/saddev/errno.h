#ifndef SADDEV_ERRNO_H
#define SADDEV_ERRNO_H

/*
 * Error codes shared by the kernel and user programs. System calls return a
 * negative code on failure, e.g. -ENOENT. The numbers follow the common
 * Unix assignment where one exists.
 */
#define EPERM         1  /* operation not permitted */
#define ENOENT        2  /* no such file or directory */
#define ESRCH         3  /* no such process */
#define EIO           5  /* input/output error */
#define ENOEXEC       8  /* not a valid executable */
#define EBADF         9  /* bad file descriptor */
#define ECHILD       10  /* no such child process */
#define EAGAIN       11  /* try again */
#define ENOMEM       12  /* out of memory */
#define EFAULT       14  /* bad address */
#define EEXIST       17  /* already exists */
#define ENOTDIR      20  /* not a directory */
#define EISDIR       21  /* is a directory */
#define EINVAL       22  /* invalid argument */
#define EMFILE       24  /* too many open files */
#define EROFS        30  /* read-only file system */
#define EPIPE        32  /* the other end is closed */
#define ENAMETOOLONG 36  /* name too long */
#define ENOSYS       38  /* no such system call */
#define EMSGSIZE     90  /* message too long */

#endif
