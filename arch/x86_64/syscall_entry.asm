; System call entry and the first entry into user mode.

bits 64
default rel

extern syscall_dispatch

section .bss
; Top of the current thread's kernel stack; see arch_set_kernel_stack().
global syscall_kernel_stack
syscall_kernel_stack: resq 1
; The user stack pointer has to be parked somewhere while rsp is switched,
; and at that point no register is free. A plain variable is enough on one
; CPU with interrupts off. (With several CPUs this becomes per-CPU data
; reached through the gs segment and `swapgs`.)
syscall_user_stack: resq 1

section .text

; On entry (set up by the CPU): rcx = user rip, r11 = user rflags,
; interrupts off, rsp still the USER stack.
; Convention: rax = number, arguments in rdi, rsi, rdx, r10, r8, r9;
; result in rax. rcx and r11 are destroyed, everything else is preserved.
global syscall_entry
syscall_entry:
    mov [syscall_user_stack], rsp
    mov rsp, [syscall_kernel_stack]

    ; Build a struct syscall_frame. Copying the user rsp onto the kernel
    ; stack makes it part of this thread's state, so the shared variable
    ; may be reused by another thread as soon as interrupts are on.
    push qword [syscall_user_stack]
    push r11
    push rcx
    push rbp
    push rbx
    push r12
    push r13
    push r14
    push r15
    push r9
    push r8
    push r10
    push rdx
    push rsi
    push rdi
    push rax

    ; 16 pushes from a 16-byte aligned stack top: rsp is aligned for the call.
    mov rdi, rsp
    xor ebp, ebp            ; stack traces of kernel code stop here
    sti
    call syscall_dispatch
    cli

    pop rax
    pop rdi
    pop rsi
    pop rdx
    pop r10
    pop r8
    pop r9
    pop r15
    pop r14
    pop r13
    pop r12
    pop rbx
    pop rbp
    pop rcx                 ; sysret takes rip from rcx
    pop r11                 ; and rflags from r11
    pop rsp                 ; back to the user stack
    o64 sysret

; _Noreturn void arch_enter_user(uintptr_t entry,          ; rdi
;                                uintptr_t stack_pointer,  ; rsi
;                                uint64_t argument0,       ; rdx
;                                uint64_t argument1);      ; rcx
;
; There is no "jump to ring 3" instruction. Instead we build the frame an
; interrupt from user mode would have left and "return" from it with iretq.
%define USER_DATA_SELECTOR 0x1B
%define USER_CODE_SELECTOR 0x23
%define INITIAL_RFLAGS     0x202   ; interrupts enabled (bit 1 is always set)
global arch_enter_user
arch_enter_user:
    cli
    push USER_DATA_SELECTOR ; ss
    push rsi                ; rsp
    push INITIAL_RFLAGS
    push USER_CODE_SELECTOR ; cs
    push rdi                ; rip

    mov rdi, rdx
    mov rsi, rcx
    ; Do not hand kernel values to the program in the other registers.
    xor eax, eax
    xor ebx, ebx
    xor ecx, ecx
    xor edx, edx
    xor ebp, ebp
    xor r8d, r8d
    xor r9d, r9d
    xor r10d, r10d
    xor r11d, r11d
    xor r12d, r12d
    xor r13d, r13d
    xor r14d, r14d
    xor r15d, r15d
    iretq

section .note.GNU-stack noalloc noexec nowrite progbits
