; Kernel context switch.
;
; A switch happens inside a function call, so by the calling convention the
; caller already expects rax, rcx, rdx, rsi, rdi and r8-r11 to be clobbered.
; Only the callee-saved registers have to survive: rbx, rbp, r12-r15, and
; the stack pointer itself. They are pushed on the old stack, the stack
; pointer is swapped, and the same registers are popped from the new stack.

bits 64
default rel

section .text

; void arch_context_switch(void **save, void *load);
;                          rdi          rsi
global arch_context_switch
arch_context_switch:
    push rbp
    push rbx
    push r12
    push r13
    push r14
    push r15

    mov [rdi], rsp
    mov rsp, rsi

    pop r15
    pop r14
    pop r13
    pop r12
    pop rbx
    pop rbp
    ; This pops the return address the *resumed* context pushed when it
    ; called arch_context_switch, or, for a new context, the entry point
    ; arch_context_create planted.
    ret

section .note.GNU-stack noalloc noexec nowrite progbits
