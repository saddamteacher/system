/*
 * C side of the boot entry: declares the Limine requests, validates what the
 * bootloader gave us, prepares the early console and hands off to kmain().
 */
#include <arch/boot.h>
#include <arch/cpu.h>
#include <arch/early_console.h>

#include <stdint.h>

#include "../early_console.h"
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

LIMINE_SECTION(".limine_requests_end")
static volatile uint64_t requests_end_marker[] = LIMINE_REQUESTS_END_MARKER;

static void early_print(const char *text)
{
    size_t length = 0;
    while (text[length] != '\0') {
        length++;
    }
    arch_early_console_write(text, length);
}

/* Called from _start in entry.asm. */
_Noreturn void arch_entry(void);

_Noreturn void arch_entry(void)
{
    early_console_init();

    if (base_revision[LIMINE_BASE_REVISION_INDEX] != 0) {
        /* The layout of everything the bootloader passes us depends on the
         * revision, so continuing would mean reading garbage. */
        early_print("boot: bootloader does not support the required Limine base revision\n");
        arch_halt_forever();
    }

    kmain();
}
