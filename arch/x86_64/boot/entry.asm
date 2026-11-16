; First instructions of the kernel. Limine jumps here in 64-bit long mode with
; paging enabled, interrupts disabled and a valid stack already set up.

bits 64
default rel

extern arch_entry

section .text
global _start
_start:
    cli

    ; A zero frame pointer marks the outermost stack frame, so stack walkers
    ; (GDB, our future panic backtrace) know where to stop.
    xor ebp, ebp

    ; The System V ABI requires rsp to be 16-byte aligned right before a call.
    ; Do not rely on the bootloader for that; compilers assume it when they
    ; emit aligned stack accesses.
    and rsp, -16

    call arch_entry

    ; arch_entry never returns. If it does anyway, stop here.
.hang:
    hlt
    jmp .hang

; Tells the linker this object does not need an executable stack.
section .note.GNU-stack noalloc noexec nowrite progbits
