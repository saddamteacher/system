#ifndef ARCH_TRAP_H
#define ARCH_TRAP_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/*
 * Traps: CPU exceptions and interrupts, described in architecture-neutral
 * terms so the portable kernel can report them.
 */

/* Saved CPU state at the moment of a trap. Opaque outside the arch layer. */
struct arch_trap_frame;

enum arch_trap_kind {
    ARCH_TRAP_FATAL,      /* the kernel cannot continue */
    ARCH_TRAP_BREAKPOINT, /* debugging aid; execution resumes afterwards */
};

struct arch_trap_info {
    enum arch_trap_kind kind;
    const char *name; /* human readable, e.g. "Divide Error (#DE)" */
    uint64_t vector;
    uint64_t error_code;
    bool from_user;         /* the trap interrupted user mode code */
    bool has_fault_address; /* true for memory access faults */
    uintptr_t fault_address;
};

/*
 * Upcall, implemented by the kernel (kernel/core/panic.c). The arch trap
 * entry calls it, with interrupts disabled, for every trap it does not
 * handle itself. Returning resumes the interrupted code.
 */
void kernel_handle_trap(const struct arch_trap_frame *frame, const struct arch_trap_info *info);

struct arch_register {
    const char *name;
    uint64_t value;
};

/* Upper bound on what arch_trap_registers() produces, for sizing buffers. */
#define ARCH_TRAP_REGISTER_MAX 32

/* Copies the saved registers into `out`; returns how many were written. */
size_t arch_trap_registers(const struct arch_trap_frame *frame, struct arch_register *out, size_t capacity);

/* Where the trapped code was executing, and its frame pointer. */
uintptr_t arch_trap_pc(const struct arch_trap_frame *frame);
uintptr_t arch_trap_frame_pointer(const struct arch_trap_frame *frame);

/*
 * Stack unwinding through the frame-pointer chain. Given the frame pointer
 * of one function, stores its return address and moves *frame_pointer to the
 * caller's frame. Returns false at the end of the chain or if the frame
 * pointer does not look like a kernel stack address.
 */
bool arch_stack_walk(uintptr_t *frame_pointer, uintptr_t *return_address);

#endif
