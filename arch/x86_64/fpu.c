/*
 * x87/SSE state handling with fxsave/fxrstor, which store and load all of
 * it (x87 stack, xmm0-15, control words) in a 512-byte area.
 */
#include <arch/fpu.h>

#include <stdint.h>

#include "fpu.h"

#define FXSAVE_AREA_SIZE 512

/* CR0: MP makes `wait` honour the task-switched flag, EM would make every
 * x87 instruction trap (for software emulation), NE selects native error
 * reporting. */
#define CR0_MP (1ULL << 1)
#define CR0_EM (1ULL << 2)
#define CR0_NE (1ULL << 5)
/* CR4: the OS promises to save SSE state (without this bit SSE instructions
 * are illegal) and to handle SSE exceptions. */
#define CR4_OSFXSR     (1ULL << 9)
#define CR4_OSXMMEXCPT (1ULL << 10)

/* Offsets inside the fxsave area, and the power-on default values: all
 * exceptions masked, round to nearest. */
#define FXSAVE_FCW_OFFSET   0
#define FXSAVE_MXCSR_OFFSET 24
#define DEFAULT_FCW   0x037F
#define DEFAULT_MXCSR 0x1F80

void fpu_init(void)
{
    uint64_t cr0, cr4;
    __asm__ volatile("mov %%cr0, %0" : "=r"(cr0));
    cr0 = (cr0 & ~CR0_EM) | CR0_MP | CR0_NE;
    __asm__ volatile("mov %0, %%cr0" : : "r"(cr0));

    __asm__ volatile("mov %%cr4, %0" : "=r"(cr4));
    cr4 |= CR4_OSFXSR | CR4_OSXMMEXCPT;
    __asm__ volatile("mov %0, %%cr4" : : "r"(cr4));

    __asm__ volatile("fninit");
}

size_t arch_fpu_state_size(void)
{
    return FXSAVE_AREA_SIZE;
}

void arch_fpu_state_init(void *state)
{
    uint8_t *bytes = state;
    for (unsigned i = 0; i < FXSAVE_AREA_SIZE; i++) {
        bytes[i] = 0;
    }
    bytes[FXSAVE_FCW_OFFSET] = DEFAULT_FCW & 0xFF;
    bytes[FXSAVE_FCW_OFFSET + 1] = DEFAULT_FCW >> 8;
    bytes[FXSAVE_MXCSR_OFFSET] = DEFAULT_MXCSR & 0xFF;
    bytes[FXSAVE_MXCSR_OFFSET + 1] = DEFAULT_MXCSR >> 8;
}

void arch_fpu_save(void *state)
{
    __asm__ volatile("fxsave64 (%0)" : : "r"(state) : "memory");
}

void arch_fpu_restore(const void *state)
{
    __asm__ volatile("fxrstor64 (%0)" : : "r"(state) : "memory");
}
