#ifndef ARCH_X86_64_GDT_H
#define ARCH_X86_64_GDT_H

/*
 * Segment selectors: byte offsets into the GDT. The low two bits of a
 * selector are the requested privilege level, hence `| 3` for ring 3.
 *
 * The order is not arbitrary. The syscall/sysret instructions (Phase 5)
 * compute selectors by adding fixed offsets to a base, and require
 * "user data" to sit directly before "user code".
 */
#define GDT_KERNEL_CODE 0x08
#define GDT_KERNEL_DATA 0x10
#define GDT_USER_DATA   (0x18 | 3)
#define GDT_USER_CODE   (0x20 | 3)
#define GDT_TSS         0x28

/*
 * Interrupt Stack Table slots (1-based, as the CPU numbers them). A gate
 * that names a slot makes the CPU switch to that known-good stack before
 * running the handler, whatever state the interrupted stack was in.
 */
#define IST_DOUBLE_FAULT 1
#define IST_NMI          2

/* Builds and loads the GDT and TSS. Boot CPU, once, interrupts disabled. */
void gdt_init(void);

#endif
