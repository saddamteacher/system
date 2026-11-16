#ifndef ARCH_BOOT_H
#define ARCH_BOOT_H

/*
 * Boot handoff contract between the architecture layer and the kernel.
 *
 * The architecture's boot code runs first, brings the CPU into a state where
 * portable C can execute, and then calls kmain(). kmain() is implemented by
 * the portable kernel (kernel/core) and must never return.
 */
_Noreturn void kmain(void);

#endif
