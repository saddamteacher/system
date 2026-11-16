#ifndef ARCH_X86_64_MSR_H
#define ARCH_X86_64_MSR_H

#include <stdint.h>

/*
 * Model Specific Registers: CPU configuration registers addressed by number
 * and accessed with rdmsr/wrmsr (register number in ecx, value in edx:eax).
 */

static inline uint64_t msr_read(uint32_t msr)
{
    uint32_t low, high;
    __asm__ volatile("rdmsr" : "=a"(low), "=d"(high) : "c"(msr));
    return ((uint64_t)high << 32) | low;
}

static inline void msr_write(uint32_t msr, uint64_t value)
{
    __asm__ volatile("wrmsr" : : "a"((uint32_t)value), "d"((uint32_t)(value >> 32)), "c"(msr));
}

#endif
