/*
 * Portable kernel entry point and composition root: the one place that knows
 * about every subsystem and wires them together in the right order.
 */
#include <arch/boot.h>
#include <arch/cpu.h>
#include <core/cmdline.h>
#include <core/log.h>
#include <core/panic.h>
#include <drivers/serial/serial.h>
#include <mm/heap.h>
#include <mm/pmm.h>
#include <mm/vmm.h>
#include <tests/tests.h>

#define BOOT_STACK_PAGES 16

static void run_requested_test(const char *cmdline)
{
    char name[32];
    if (!cmdline_get(cmdline, "test", name, sizeof(name))) {
        return;
    }

    log_info("test: running '%s'", name);
    switch (tests_run(name)) {
    case TEST_PASSED:
        log_info("test: '%s' passed", name);
        break;
    case TEST_FAILED:
        panic("test '%s' failed", name);
    case TEST_UNKNOWN:
        panic("unknown test '%s'", name);
    }
}

static void memory_init(void)
{
    /* Each layer is built on the one before it: frames, then pages mapped
     * onto frames, then arbitrary sizes carved out of pages. */
    pmm_init();
    vmm_init();
    heap_init();

    struct pmm_stats stats = pmm_stats();
    log_info("mm: %zu of %zu frames free after initialisation", stats.free_frames, stats.total_frames);
}

/* Second half of boot, running on a stack the kernel allocated itself. */
static void kmain_on_kernel_stack(void)
{
    const char *cmdline = arch_boot_cmdline();
    log_info("boot: command line \"%s\"", cmdline);

    log_info("hello from the kernel");
    run_requested_test(cmdline);

    /* Lets `make check` finish immediately; a no-op under `make run`. */
    arch_debug_exit(ARCH_DEBUG_EXIT_SUCCESS);
    arch_halt_forever();
}

_Noreturn void kmain(void)
{
    /* Logging first: everything after this can report what it does. */
    serial_init();
    log_set_sink(serial_write);
    log_info("Lumen kernel starting");

    const char *problem = arch_boot_problem();
    if (problem != NULL) {
        panic("boot: %s", problem);
    }

    arch_cpu_init();
    log_info("cpu: descriptor tables and trap handlers installed");

    memory_init();

    /* Until now we ran on the bootloader's stack, which has no guard page
     * and lives in memory we want to reclaim eventually. */
    void *stack_top = vmm_alloc_kernel_stack(BOOT_STACK_PAGES);
    if (stack_top == NULL) {
        panic("boot: out of memory allocating the kernel stack");
    }
    arch_call_on_stack(stack_top, kmain_on_kernel_stack);
}
