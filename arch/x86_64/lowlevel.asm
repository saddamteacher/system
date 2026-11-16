; Operations that cannot be expressed in C or inline assembly.

bits 64
default rel

section .text

; void gdt_load(const struct descriptor_table_pointer *pointer,   ; rdi
;               uint16_t code_selector,                           ; rsi
;               uint16_t data_selector);                          ; rdx
;
; Loads a new GDT and makes every segment register refer to it. Segment
; registers cache the descriptor they were loaded from, so the new table has
; no effect until each register is reloaded.
global gdt_load
gdt_load:
    lgdt [rdi]

    mov ds, dx
    mov es, dx
    mov ss, dx
    ; fs and gs are not used as segments in 64-bit mode; their bases are set
    ; through MSRs later. Load the null selector.
    xor eax, eax
    mov fs, ax
    mov gs, ax

    ; CS cannot be written with mov. A far return pops rip and then cs, so we
    ; turn our own return into a far return: [rsp] = return address,
    ; [rsp+8] = new code selector.
    pop rax
    push rsi
    push rax
    retfq

; _Noreturn void arch_call_on_stack(void *stack_top,        ; rdi
;                                   void (*entry)(void));  ; rsi
global arch_call_on_stack
arch_call_on_stack:
    mov rsp, rdi
    ; New stack, new outermost frame: stack traces end here.
    xor ebp, ebp
    call rsi
.hang:
    hlt
    jmp .hang

section .note.GNU-stack noalloc noexec nowrite progbits
