/*
 * Portable kernel entry point. Runs after the architecture boot code.
 */
#include <arch/boot.h>
#include <arch/cpu.h>
#include <arch/early_console.h>

_Noreturn void kmain(void)
{
    static const char hello[] = "hello from the kernel\n";
    arch_early_console_write(hello, sizeof(hello) - 1);

    /* Lets `make check` finish immediately; a no-op under `make run`. */
    arch_debug_exit(ARCH_DEBUG_EXIT_SUCCESS);

    arch_halt_forever();
}
