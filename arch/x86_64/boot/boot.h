#ifndef ARCH_X86_64_BOOT_BOOT_H
#define ARCH_X86_64_BOOT_BOOT_H

#include <stdint.h>

/* Boot information needed by the rest of arch/x86_64 only. */

/* Offset of the higher half direct map: virtual = physical + offset. */
uint64_t boot_hhdm_offset(void);

/* Physical and virtual address of the start of the kernel image. */
uint64_t boot_kernel_physical_base(void);
uint64_t boot_kernel_virtual_base(void);

#endif
