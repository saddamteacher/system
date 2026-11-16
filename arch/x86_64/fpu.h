#ifndef ARCH_X86_64_FPU_H
#define ARCH_X86_64_FPU_H

/* Enables the x87 and SSE units so that user programs can use them. */
void fpu_init(void);

#endif
