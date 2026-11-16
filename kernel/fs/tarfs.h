#ifndef FS_TARFS_H
#define FS_TARFS_H

#include <stddef.h>

struct filesystem;

/*
 * Read-only file system over a tar archive held in memory: the initial
 * RAM file system ("initramfs") the bootloader loads next to the kernel.
 *
 * Returns the file system to mount, or NULL if the archive is invalid or
 * memory ran out. `archive` must stay mapped forever.
 */
const struct filesystem *tarfs_create(const void *archive, size_t size);

#endif
