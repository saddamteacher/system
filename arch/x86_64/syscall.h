#ifndef ARCH_X86_64_SYSCALL_H
#define ARCH_X86_64_SYSCALL_H

#include <stdint.h>

/* Programs the CPU so that the `syscall` instruction enters the kernel. */
void syscall_init(void);

/* Kernel stack the syscall entry code switches to. Defined in
 * syscall_entry.asm, written by arch_set_kernel_stack(). */
extern uint64_t syscall_kernel_stack;

/* Register state saved by syscall_entry (syscall_entry.asm); the field
 * order is the reverse of its push order. */
struct syscall_frame {
    uint64_t rax; /* system call number in, return value out */
    uint64_t rdi, rsi, rdx, r10, r8, r9; /* arguments 0-5 */
    uint64_t r15, r14, r13, r12, rbx, rbp;
    uint64_t rip;    /* user return address (the CPU put it in rcx) */
    uint64_t rflags; /* user flags (the CPU put them in r11) */
    uint64_t rsp;    /* user stack pointer */
};

/* Called from syscall_entry. */
void syscall_dispatch(struct syscall_frame *frame);

#endif
