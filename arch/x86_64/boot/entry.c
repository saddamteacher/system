/*
 * C side of the boot entry: declares the Limine requests and exposes what
 * the bootloader answered through the <arch/boot.h> interface.
 *
 * State: the request structures are written by the bootloader before the
 * kernel starts and are read-only afterwards.
 */
#include <arch/boot.h>

#include <stddef.h>
#include <stdint.h>

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
