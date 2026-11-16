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

/* Every request id starts with these two words, followed by two words that
 * identify the specific request. */
#define LIMINE_COMMON_MAGIC 0xc7b1dd30df4c8b88ULL, 0x0a82e883a194f07bULL

/*
 * Executable command line: the `cmdline:` value of the boot menu entry.
 * `response` stays NULL if the bootloader does not know the request.
 */
#define LIMINE_EXECUTABLE_CMDLINE_REQUEST_ID \
    { LIMINE_COMMON_MAGIC, 0x4b161536e598651eULL, 0xb390ad4a2f1f303aULL }

struct limine_executable_cmdline_response {
    uint64_t revision;
    char *cmdline;
};

struct limine_executable_cmdline_request {
    uint64_t id[4];
    uint64_t revision;
    struct limine_executable_cmdline_response *response;
};

/*
 * Higher Half Direct Map: the bootloader maps physical memory at
 * `offset + physical address`, so the kernel can reach any frame before it
 * has page tables of its own.
 */
#define LIMINE_HHDM_REQUEST_ID \
    { LIMINE_COMMON_MAGIC, 0x48dcf1cb8ad2b852ULL, 0x63984e959a98244bULL }

struct limine_hhdm_response {
    uint64_t revision;
    uint64_t offset;
};

struct limine_hhdm_request {
    uint64_t id[4];
    uint64_t revision;
    struct limine_hhdm_response *response;
};

/* Physical memory map. Entries are sorted by base address; usable and
 * bootloader-reclaimable entries are page aligned and do not overlap. */
#define LIMINE_MEMMAP_REQUEST_ID \
    { LIMINE_COMMON_MAGIC, 0x67cf3d9d378a806fULL, 0xe304acdfc50c3c62ULL }

#define LIMINE_MEMMAP_USABLE                 0
#define LIMINE_MEMMAP_RESERVED               1
#define LIMINE_MEMMAP_ACPI_RECLAIMABLE       2
#define LIMINE_MEMMAP_ACPI_NVS               3
#define LIMINE_MEMMAP_BAD_MEMORY             4
#define LIMINE_MEMMAP_BOOTLOADER_RECLAIMABLE 5
#define LIMINE_MEMMAP_EXECUTABLE_AND_MODULES 6
#define LIMINE_MEMMAP_FRAMEBUFFER            7

struct limine_memmap_entry {
    uint64_t base;
    uint64_t length;
    uint64_t type;
};

struct limine_memmap_response {
    uint64_t revision;
    uint64_t entry_count;
    struct limine_memmap_entry **entries;
};

struct limine_memmap_request {
    uint64_t id[4];
    uint64_t revision;
    struct limine_memmap_response *response;
};

/* Where the kernel image was loaded: the physical address that corresponds
 * to the virtual address it was linked at. */
#define LIMINE_EXECUTABLE_ADDRESS_REQUEST_ID \
    { LIMINE_COMMON_MAGIC, 0x71ba76863cc55f63ULL, 0xb2644a48c516a487ULL }

struct limine_executable_address_response {
    uint64_t revision;
    uint64_t physical_base;
    uint64_t virtual_base;
};

struct limine_executable_address_request {
    uint64_t id[4];
    uint64_t revision;
    struct limine_executable_address_response *response;
};

#endif
