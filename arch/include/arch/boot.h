#ifndef ARCH_BOOT_H
#define ARCH_BOOT_H

#include <stddef.h>
#include <stdint.h>

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

/*
 * Physical memory map, as reported by the firmware and bootloader. Regions
 * are sorted by base address. Only ARCH_MEMORY_USABLE may be handed out by
 * the frame allocator.
 */
enum arch_memory_type {
    ARCH_MEMORY_USABLE,
    ARCH_MEMORY_RESERVED,         /* firmware, devices, holes: do not touch */
    ARCH_MEMORY_ACPI_RECLAIMABLE, /* ACPI tables */
    ARCH_MEMORY_ACPI_NVS,
    ARCH_MEMORY_BAD,
    /* Data the bootloader handed to us (memory map, command line, the boot
     * stack). Ordinary RAM, but still in use. */
    ARCH_MEMORY_BOOT_RECLAIMABLE,
    ARCH_MEMORY_KERNEL_IMAGE,     /* the kernel executable and boot modules */
    ARCH_MEMORY_FRAMEBUFFER,
};

struct arch_memory_region {
    uint64_t base;
    uint64_t length;
    enum arch_memory_type type;
};

size_t arch_boot_memory_region_count(void);
struct arch_memory_region arch_boot_memory_region(size_t index);

#endif
