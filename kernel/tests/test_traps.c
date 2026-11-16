/* Tests that raise CPU exceptions on purpose. */
#include "tests_internal.h"

#include <arch/cpu.h>
#include <mm/vmm.h>

bool test_breakpoint(void)
{
    /* Exercises the full round trip through the interrupt entry code: if any
     * register or the stack were not restored exactly, we would not get back
     * here with `marker` intact. */
    volatile unsigned long marker = 0x1122334455667788UL;
    arch_breakpoint();
    return marker == 0x1122334455667788UL;
}

bool test_divide_by_zero(void)
{
    /*
     * Division by zero is undefined behaviour in C, so the compiler may do
     * anything, including not dividing at all. Both operands are volatile on
     * purpose: with a constant dividend of 1, clang computes "1 / x" using
     * comparisons (the result is x for x = 1 or -1, otherwise 0) and never
     * emits a division instruction, so nothing traps.
     */
    volatile int dividend = 1;
    volatile int divisor = 0;
    volatile int result = dividend / divisor;

    /* Reaching this line means the CPU exception did not stop us. */
    (void)result;
    return false;
}

bool test_page_fault(void)
{
    /* An address in the lower half that nothing maps. volatile so the
     * compiler performs the read instead of reasoning about it. */
    volatile unsigned char *volatile unmapped = (volatile unsigned char *)0xdead0000UL;
    volatile unsigned char value = *unmapped;

    (void)value;
    return false;
}

bool test_write_rodata(void)
{
    /* The kernel maps its constants read-only. Cast the const away through
     * a volatile pointer so the compiler really emits the write. */
    static const char constant[] = "read-only";
    volatile char *volatile writable = (volatile char *)(uintptr_t)constant;
    *writable = 'X';
    return false;
}

bool test_use_after_unmap(void)
{
    volatile unsigned char *page = vmm_alloc_pages(1, ARCH_MAP_WRITE);
    if (page == NULL) {
        return false;
    }
    /* Touch it first so the translation is cached in the TLB. If unmapping
     * forgot to invalidate that entry, the access below would succeed. */
    page[0] = 0x5A;
    vmm_free_pages((void *)page, 1);

    volatile unsigned char value = page[0];
    (void)value;
    return false;
}

/* Recurses until the stack runs into its guard page. The result of the
 * call is used afterwards so this cannot be turned into a loop, and the exit
 * condition (never reached in practice) keeps the compiler from rejecting
 * the function as infinitely recursive. */
static unsigned long recurse_until_overflow(volatile unsigned long depth)
{
    volatile char frame_padding[256];
    frame_padding[0] = (char)depth;
    if (depth == ~0UL) {
        return 0;
    }
    return recurse_until_overflow(depth + 1) + (unsigned long)frame_padding[0];
}

bool test_stack_overflow(void)
{
    /* The page fault on the guard page cannot be delivered, because the CPU
     * needs the (exhausted) stack to do so. That escalates to a double
     * fault, which runs on its own IST stack. */
    return recurse_until_overflow(0) == 0;
}
