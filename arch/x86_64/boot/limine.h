#ifndef ARCH_X86_64_BOOT_LIMINE_H
#define ARCH_X86_64_BOOT_LIMINE_H

/*
 * Our own minimal definitions for the Limine boot protocol, written from the
 * protocol specification. Only what the kernel currently uses is defined.
 *
 * How the protocol works: the kernel image contains "requests", structures
 * that start with a well-known magic number. Before jumping to the kernel,
 * the bootloader scans the image for these magics and writes its answers
 * into the structures in place.
 */

#include <stdint.h>

/* Requests are only searched for between these two markers, so stray data
 * that happens to look like a magic number elsewhere cannot confuse Limine. */
#define LIMINE_REQUESTS_START_MARKER \
    { 0xf6b8f4b39de7d1aeULL, 0xfab91a6940fcb9cfULL, \
      0x785c6ed015d3e316ULL, 0x181e920a7852b9d9ULL }
#define LIMINE_REQUESTS_END_MARKER \
    { 0xadc0e0531bb10d03ULL, 0x9572709f31764c62ULL }

/*
 * Base revision tag: { magic, magic, revision }. It tells the bootloader
 * which version of the protocol the kernel was written against. A bootloader
 * that supports the revision overwrites the third word with 0.
 */
#define LIMINE_BASE_REVISION_TAG(revision) \
    { 0xf9562b2d5c95a6c8ULL, 0x6a7b384944536bdcULL, (revision) }
#define LIMINE_BASE_REVISION_INDEX 2

#endif
