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
#include <tests/tests.h>

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

    const char *cmdline = arch_boot_cmdline();
    log_info("boot: command line \"%s\"", cmdline);

    log_info("hello from the kernel");
    run_requested_test(cmdline);

    /* Lets `make check` finish immediately; a no-op under `make run`. */
    arch_debug_exit(ARCH_DEBUG_EXIT_SUCCESS);
    arch_halt_forever();
}
