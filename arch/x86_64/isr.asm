; Interrupt entry stubs.
;
; When an interrupt or exception occurs the CPU pushes ss, rsp, rflags, cs and
; rip and jumps to the IDT gate. It does not tell the handler which vector
; fired and saves no general purpose registers. So every vector gets its own
; tiny stub that records the vector number, and all stubs share one routine
; that saves the registers and calls C.

bits 64
default rel

extern trap_dispatch

%define VECTOR_COUNT 256

section .text

; Only some exceptions push an error code. To give C one frame layout for all
; vectors, the stubs of the others push a dummy 0 in its place.
%define HAS_ERROR_CODE(v) ((v) = 8 || ((v) >= 10 && (v) <= 14) || (v) = 17 || (v) = 21 || (v) = 29 || (v) = 30)

%assign vector 0
%rep VECTOR_COUNT
isr_stub_%+vector:
    %if !HAS_ERROR_CODE(vector)
        push 0
    %endif
    push vector
    jmp isr_common
    %assign vector vector + 1
%endrep

; Stack on entry, from high to low addresses:
;   ss, rsp, rflags, cs, rip   pushed by the CPU
;   error code                 pushed by the CPU or the stub
;   vector                     pushed by the stub
; After the pushes below, rsp points at a struct arch_trap_frame (trap.h).
isr_common:
    push rax
    push rbx
    push rcx
    push rdx
    push rsi
    push rdi
    push rbp
    push r8
    push r9
    push r10
    push r11
    push r12
    push r13
    push r14
    push r15

    ; C code assumes the direction flag is clear; the interrupted code may
    ; have set it.
    cld

    ; The CPU aligns rsp to 16 bytes before pushing its 5 words, and we pushed
    ; 2 + 15 more: 22 words in total, so rsp is 16-byte aligned again here, as
    ; the ABI requires at a call.
    mov rdi, rsp
    call trap_dispatch

    pop r15
    pop r14
    pop r13
    pop r12
    pop r11
    pop r10
    pop r9
    pop r8
    pop rbp
    pop rdi
    pop rsi
    pop rdx
    pop rcx
    pop rbx
    pop rax

    ; Drop vector and error code; iretq expects rip on top.
    add rsp, 16
    iretq

section .rodata

; Addresses of all stubs, indexed by vector, for idt_init().
global isr_stub_table
isr_stub_table:
%assign vector 0
%rep VECTOR_COUNT
    dq isr_stub_%+vector
    %assign vector vector + 1
%endrep

section .note.GNU-stack noalloc noexec nowrite progbits
