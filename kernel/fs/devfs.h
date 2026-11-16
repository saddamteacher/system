#ifndef FS_DEVFS_H
#define FS_DEVFS_H

struct filesystem;

/*
 * Device file system: devices that programs use through the file interface.
 *
 *   console   write: text to the kernel log (serial port)
 *             read:  characters typed on the keyboard; blocks until one
 *                    is available
 */
const struct filesystem *devfs_filesystem(void);

#endif
