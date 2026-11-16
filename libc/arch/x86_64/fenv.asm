; Rounding mode control. x86 has two floating point units, each with its own
; copy of the rounding mode: SSE (in MXCSR, bits 13-14) and x87 (in the
; control word, bits 10-11). Both are kept in step.

bits 64
default rel

%define X87_ROUNDING_MASK   0x0C00
%define MXCSR_ROUNDING_SHIFT 3      ; MXCSR holds the same two bits, 3 higher

section .text

; int fegetround(void);
global fegetround
fegetround:
    sub rsp, 8
    stmxcsr [rsp]
    mov eax, [rsp]
    shr eax, MXCSR_ROUNDING_SHIFT
    and eax, X87_ROUNDING_MASK
    add rsp, 8
    ret

; int fesetround(int mode);
global fesetround
fesetround:
    test edi, ~X87_ROUNDING_MASK
    jnz .invalid
    sub rsp, 8

    stmxcsr [rsp]
    mov eax, [rsp]
    and eax, ~(X87_ROUNDING_MASK << MXCSR_ROUNDING_SHIFT)
    mov edx, edi
    shl edx, MXCSR_ROUNDING_SHIFT
    or eax, edx
    mov [rsp], eax
    ldmxcsr [rsp]

    fnstcw [rsp]
    mov ax, [rsp]
    and ax, ~X87_ROUNDING_MASK
    or ax, di
    mov [rsp], ax
    fldcw [rsp]

    add rsp, 8
    xor eax, eax
    ret
.invalid:
    mov eax, 1
    ret

section .note.GNU-stack noalloc noexec nowrite progbits
