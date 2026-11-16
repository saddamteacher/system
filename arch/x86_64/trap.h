#ifndef ARCH_X86_64_TRAP_H
#define ARCH_X86_64_TRAP_H

#include <stdint.h>

#define VECTOR_NMI          2
#define VECTOR_BREAKPOINT   3
#define VECTOR_DOUBLE_FAULT 8
#define VECTOR_PAGE_FAULT   14
#define VECTOR_EXCEPTION_COUNT 32 /* vectors 0-31 are reserved for the CPU */

/*
 * Register state saved on the stack at interrupt entry. The field order is
 * the reverse of the push order in isr.asm; the two must be kept in sync.
 */
struct arch_trap_frame {
    uint64_t r15, r14, r13, r12, r11, r10, r9, r8;
    uint64_t rbp, rdi, rsi, rdx, rcx, rbx, rax;
    uint64_t vector;
    uint64_t error_code;
    /* Pushed by the CPU. */
    uint64_t rip;
    uint64_t cs;
    uint64_t rflags;
    uint64_t rsp;
    uint64_t ss;
};

/* Called from isr_common in isr.asm. */
void trap_dispatch(struct arch_trap_frame *frame);

#endif
