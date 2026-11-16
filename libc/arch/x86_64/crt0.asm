; Program entry point. The kernel starts a program here with argc in rdi,
; argv in rsi and a 16-byte aligned stack.

bits 64
default rel

extern __libc_start

section .text
global _start
_start:
    xor ebp, ebp        ; outermost frame, for stack traces
    and rsp, -16
    call __libc_start   ; calls main and exits; never returns
    ud2

section .note.GNU-stack noalloc noexec nowrite progbits
