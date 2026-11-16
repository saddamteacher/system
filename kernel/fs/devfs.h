#ifndef FS_DEVFS_H
#define FS_DEVFS_H

struct filesystem;

/*
 * Device file system: devices that programs use through the file interface.
 *
 *   console   write: text to the kernel log (serial port)
 *             read:  characters typed on the keyboard; blocks until one
 *                    is available
 *   keyboard  read:  struct sd_key_event records (<saddev/input.h>)
 *   mouse     read:  struct sd_mouse_event records
 *
 * The keyboard has one queue of events: `console` and `keyboard` both take
 * from it, so only one of them should be read at a time.
 */
const struct filesystem *devfs_filesystem(void);

#endif
