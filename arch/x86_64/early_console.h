#ifndef ARCH_X86_64_EARLY_CONSOLE_H
#define ARCH_X86_64_EARLY_CONSOLE_H

/* Programs the COM1 UART. Must run before arch_early_console_write(). */
void early_console_init(void);

#endif
