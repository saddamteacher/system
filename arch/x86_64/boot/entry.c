/*
 * C side of the boot entry: declares the Limine requests and exposes what
 * the bootloader answered through the <arch/boot.h> interface.
 *
 * State: the request structures are written by the bootloader before the
 * kernel starts and are read-only afterwards.
 */
#include <arch/boot.h>

#include "boot.h"
#include "limine.h"

#define LIMINE_BASE_REVISION 3

/*
 * `used` keeps the compiler from dropping these (nothing in C reads the
 * markers); `volatile` stops it from assuming the initial values at compile
 * time, because the bootloader rewrites them before the kernel runs.
 */
#define LIMINE_SECTION(name) __attribute__((used, section(name)))

LIMINE_SECTION(".limine_requests_start")
static volatile uint64_t requests_start_marker[] = LIMINE_REQUESTS_START_MARKER;

LIMINE_SECTION(".limine_requests")
static volatile uint64_t base_revision[] = LIMINE_BASE_REVISION_TAG(LIMINE_BASE_REVISION);

LIMINE_SECTION(".limine_requests")
static volatile struct limine_executable_cmdline_request cmdline_request = {
    .id = LIMINE_EXECUTABLE_CMDLINE_REQUEST_ID,
};

LIMINE_SECTION(".limine_requests")
static volatile struct limine_hhdm_request hhdm_request = {
    .id = LIMINE_HHDM_REQUEST_ID,
};

LIMINE_SECTION(".limine_requests")
static volatile struct limine_memmap_request memmap_request = {
    .id = LIMINE_MEMMAP_REQUEST_ID,
};

LIMINE_SECTION(".limine_requests")
static volatile struct limine_executable_address_request address_request = {
    .id = LIMINE_EXECUTABLE_ADDRESS_REQUEST_ID,
};

LIMINE_SECTION(".limine_requests")
static volatile struct limine_rsdp_request rsdp_request = {
    .id = LIMINE_RSDP_REQUEST_ID,
};

LIMINE_SECTION(".limine_requests_end")
static volatile uint64_t requests_end_marker[] = LIMINE_REQUESTS_END_MARKER;

/* Called from _start in entry.asm. */
_Noreturn void arch_entry(void);

_Noreturn void arch_entry(void)
{
    kmain();
}

const char *arch_boot_problem(void)
{
    if (base_revision[LIMINE_BASE_REVISION_INDEX] != 0) {
        /* The layout of everything the bootloader passes us depends on the
         * revision, so continuing would mean reading garbage. */
        return "bootloader does not support the required Limine base revision";
    }
    if (hhdm_request.response == NULL) {
        return "bootloader did not provide the direct map offset";
    }
    if (memmap_request.response == NULL) {
        return "bootloader did not provide a memory map";
    }
    if (address_request.response == NULL) {
        return "bootloader did not provide the kernel load address";
    }
    return NULL;
}

const char *arch_boot_cmdline(void)
{
    const struct limine_executable_cmdline_response *response = cmdline_request.response;
    if (response == NULL || response->cmdline == NULL) {
        return "";
    }
    return response->cmdline;
}

static enum arch_memory_type memory_type_from_limine(uint64_t type)
{
    switch (type) {
    case LIMINE_MEMMAP_USABLE:
        return ARCH_MEMORY_USABLE;
    case LIMINE_MEMMAP_ACPI_RECLAIMABLE:
        return ARCH_MEMORY_ACPI_RECLAIMABLE;
    case LIMINE_MEMMAP_ACPI_NVS:
        return ARCH_MEMORY_ACPI_NVS;
    case LIMINE_MEMMAP_BAD_MEMORY:
        return ARCH_MEMORY_BAD;
    case LIMINE_MEMMAP_BOOTLOADER_RECLAIMABLE:
        return ARCH_MEMORY_BOOT_RECLAIMABLE;
    case LIMINE_MEMMAP_EXECUTABLE_AND_MODULES:
        return ARCH_MEMORY_KERNEL_IMAGE;
    case LIMINE_MEMMAP_FRAMEBUFFER:
        return ARCH_MEMORY_FRAMEBUFFER;
    default:
        /* Unknown types are treated like reserved memory: never touched. */
        return ARCH_MEMORY_RESERVED;
    }
}

size_t arch_boot_memory_region_count(void)
{
    return memmap_request.response->entry_count;
}

struct arch_memory_region arch_boot_memory_region(size_t index)
{
    const struct limine_memmap_entry *entry = memmap_request.response->entries[index];
    return (struct arch_memory_region){
        .base = entry->base,
        .length = entry->length,
        .type = memory_type_from_limine(entry->type),
    };
}

uint64_t boot_hhdm_offset(void)
{
    return hhdm_request.response->offset;
}

uint64_t boot_kernel_physical_base(void)
{
    return address_request.response->physical_base;
}

uint64_t boot_kernel_virtual_base(void)
{
    return address_request.response->virtual_base;
}

uint64_t boot_rsdp_address(void)
{
    const struct limine_rsdp_response *response = rsdp_request.response;
    return response != NULL ? response->address : 0;
}
