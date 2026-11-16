#ifndef ARCH_BOOT_H
#define ARCH_BOOT_H

/*
 * Boot handoff contract between the architecture layer and the kernel.
 *
 * The architecture's boot code runs first and calls kmain() as soon as
 * portable C can execute. kmain() is implemented by the portable kernel
 * (kernel/main.c) and must never return.
 */
_Noreturn void kmain(void);

/*
 * Returns NULL if the bootloader handoff is usable, otherwise a description
 * of what is wrong. The kernel must check this before using anything else
 * that comes from the bootloader.
 */
const char *arch_boot_problem(void);

/* Kernel command line from the boot menu entry. Never NULL; may be empty. */
const char *arch_boot_cmdline(void);

#endif
