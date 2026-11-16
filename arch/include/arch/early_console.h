#ifndef ARCH_EARLY_CONSOLE_H
#define ARCH_EARLY_CONSOLE_H

#include <stddef.h>

/*
 * Earliest possible text output: polling, no interrupts, no locking, no
 * memory allocation. Initialised by the architecture boot code before
 * kmain() runs, so it is usable from the very first line of the kernel.
 */
void arch_early_console_write(const char *text, size_t length);

#endif
