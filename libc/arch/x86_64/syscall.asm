; long syscall6(long number, long a0, long a1, long a2, long a3, long a4, long a5);
;
; C passes arguments in rdi, rsi, rdx, rcx, r8, r9 and the seventh on the
; stack. The kernel wants the number in rax and arguments in rdi, rsi, rdx,
; r10, r8, r9, so everything shifts down by one. rcx cannot carry an
; argument because the syscall instruction overwrites it (and r11).

bits 64
default rel

section .text
global syscall6
syscall6:
    mov rax, rdi
    mov rdi, rsi
    mov rsi, rdx
    mov rdx, rcx
    mov r10, r8
    mov r8, r9
    mov r9, [rsp + 8]
    syscall
    ret

section .note.GNU-stack noalloc noexec nowrite progbits
