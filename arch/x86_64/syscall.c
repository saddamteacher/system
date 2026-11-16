/*
 * System call entry via the syscall/sysret instructions.
 *
 * `syscall` is a fast path into ring 0 that does almost nothing: it loads
 * rip from an MSR, saves the old rip in rcx and the flags in r11, masks
 * flags, and loads fixed code/stack segment selectors. It does not switch
 * stacks. The rest is up to syscall_entry.asm.
 */
#include "syscall.h"

#include <arch/user.h>

#include "gdt.h"
#include "msr.h"

#define MSR_EFER   0xC0000080u
#define MSR_STAR   0xC0000081u /* segment selectors for syscall and sysret */
#define MSR_LSTAR  0xC0000082u /* kernel entry point */
#define MSR_SFMASK 0xC0000084u /* flags to clear on entry */

#define EFER_SCE (1ULL << 0) /* enable syscall/sysret */

#define RFLAGS_TF (1ULL << 8)
#define RFLAGS_IF (1ULL << 9)
#define RFLAGS_DF (1ULL << 10)

/* Implemented in syscall_entry.asm. */
void syscall_entry(void);

void syscall_init(void)
{
    msr_write(MSR_EFER, msr_read(MSR_EFER) | EFER_SCE);

    /*
     * STAR bits 32-47: on syscall, CS = this value and SS = this value + 8.
     * STAR bits 48-63: on sysret, SS = this value + 8 and CS = this value
     * + 16 (both with ring 3 forced). That fixed arithmetic is why the GDT
     * has user data directly before user code.
     */
    uint64_t sysret_base = (GDT_USER_DATA & ~3ULL) - 8;
    msr_write(MSR_STAR, ((uint64_t)GDT_KERNEL_CODE << 32) | (sysret_base << 48));
    msr_write(MSR_LSTAR, (uint64_t)(uintptr_t)syscall_entry);

    /* Enter with interrupts off: for the first instructions the CPU is in
     * ring 0 but still on the user's stack, and an interrupt taken there
     * would run the handler on memory the program controls. */
    msr_write(MSR_SFMASK, RFLAGS_IF | RFLAGS_TF | RFLAGS_DF);
}

void syscall_dispatch(struct syscall_frame *frame)
{
    const uint64_t arguments[ARCH_SYSCALL_ARGUMENT_COUNT] = {
        frame->rdi, frame->rsi, frame->rdx, frame->r10, frame->r8, frame->r9,
    };
    frame->rax = (uint64_t)kernel_syscall(frame->rax, arguments);
}
