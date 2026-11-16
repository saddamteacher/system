/*
 * Trap dispatch: turns a raw x86 interrupt frame into the architecture-
 * neutral description the kernel understands.
 */
#include <arch/trap.h>

#include "trap.h"

/* Start of the higher half of the canonical address space. All kernel
 * addresses, stacks included, are above it. */
#define KERNEL_SPACE_START 0xFFFF800000000000ULL

static const char *const exception_names[VECTOR_EXCEPTION_COUNT] = {
    [0] = "Divide Error (#DE)",
    [1] = "Debug (#DB)",
    [2] = "Non-Maskable Interrupt",
    [3] = "Breakpoint (#BP)",
    [4] = "Overflow (#OF)",
    [5] = "Bound Range Exceeded (#BR)",
    [6] = "Invalid Opcode (#UD)",
    [7] = "Device Not Available (#NM)",
    [8] = "Double Fault (#DF)",
    [10] = "Invalid TSS (#TS)",
    [11] = "Segment Not Present (#NP)",
    [12] = "Stack-Segment Fault (#SS)",
    [13] = "General Protection Fault (#GP)",
    [14] = "Page Fault (#PF)",
    [16] = "x87 Floating-Point Exception (#MF)",
    [17] = "Alignment Check (#AC)",
    [18] = "Machine Check (#MC)",
    [19] = "SIMD Floating-Point Exception (#XM)",
    [20] = "Virtualization Exception (#VE)",
    [21] = "Control Protection Exception (#CP)",
    [28] = "Hypervisor Injection Exception (#HV)",
    [29] = "VMM Communication Exception (#VC)",
    [30] = "Security Exception (#SX)",
};

static const char *trap_name(uint64_t vector)
{
    if (vector >= VECTOR_EXCEPTION_COUNT) {
        /* No interrupt source is enabled yet, so nothing should arrive here. */
        return "Unexpected interrupt";
    }
    const char *name = exception_names[vector];
    return name != NULL ? name : "Reserved exception";
}

static uint64_t read_cr0(void)
{
    uint64_t value;
    __asm__ volatile("mov %%cr0, %0" : "=r"(value));
    return value;
}

/* CR2 holds the address whose access caused the most recent page fault. */
static uint64_t read_cr2(void)
{
    uint64_t value;
    __asm__ volatile("mov %%cr2, %0" : "=r"(value));
    return value;
}

static uint64_t read_cr3(void)
{
    uint64_t value;
    __asm__ volatile("mov %%cr3, %0" : "=r"(value));
    return value;
}

static uint64_t read_cr4(void)
{
    uint64_t value;
    __asm__ volatile("mov %%cr4, %0" : "=r"(value));
    return value;
}

void trap_dispatch(struct arch_trap_frame *frame)
{
    struct arch_trap_info info = {
        .kind = frame->vector == VECTOR_BREAKPOINT ? ARCH_TRAP_BREAKPOINT : ARCH_TRAP_FATAL,
        .name = trap_name(frame->vector),
        .vector = frame->vector,
        .error_code = frame->error_code,
    };
    if (frame->vector == VECTOR_PAGE_FAULT) {
        info.has_fault_address = true;
        info.fault_address = read_cr2();
    }
    kernel_handle_trap(frame, &info);
}

size_t arch_trap_registers(const struct arch_trap_frame *frame, struct arch_register *out, size_t capacity)
{
    const struct arch_register registers[] = {
        { "rax", frame->rax }, { "rbx", frame->rbx }, { "rcx", frame->rcx },
        { "rdx", frame->rdx }, { "rsi", frame->rsi }, { "rdi", frame->rdi },
        { "rbp", frame->rbp }, { "rsp", frame->rsp }, { "r8", frame->r8 },
        { "r9", frame->r9 },   { "r10", frame->r10 }, { "r11", frame->r11 },
        { "r12", frame->r12 }, { "r13", frame->r13 }, { "r14", frame->r14 },
        { "r15", frame->r15 }, { "rip", frame->rip }, { "rflags", frame->rflags },
        { "cs", frame->cs },   { "ss", frame->ss },   { "cr0", read_cr0() },
        { "cr2", read_cr2() }, { "cr3", read_cr3() }, { "cr4", read_cr4() },
    };
    _Static_assert(sizeof(registers) / sizeof(registers[0]) <= ARCH_TRAP_REGISTER_MAX,
                   "ARCH_TRAP_REGISTER_MAX is too small");

    size_t count = sizeof(registers) / sizeof(registers[0]);
    if (count > capacity) {
        count = capacity;
    }
    for (size_t i = 0; i < count; i++) {
        out[i] = registers[i];
    }
    return count;
}

uintptr_t arch_trap_pc(const struct arch_trap_frame *frame)
{
    return frame->rip;
}

uintptr_t arch_trap_frame_pointer(const struct arch_trap_frame *frame)
{
    return frame->rbp;
}

bool arch_stack_walk(uintptr_t *frame_pointer, uintptr_t *return_address)
{
    uintptr_t fp = *frame_pointer;

    /* A corrupted chain must not make the unwinder itself fault, so only
     * follow pointers that could be a kernel stack slot. _start zeroes rbp,
     * which ends the chain here. */
    if (fp < KERNEL_SPACE_START || (fp % sizeof(uintptr_t)) != 0) {
        return false;
    }

    /* Every function compiled with frame pointers starts with
     * `push rbp; mov rbp, rsp`, so [rbp] is the caller's rbp and [rbp+8] the
     * return address pushed by `call`. */
    const uintptr_t *frame = (const uintptr_t *)fp;
    *frame_pointer = frame[0];
    *return_address = frame[1];
    return *return_address != 0;
}
