#include <arch/context.h>

#include <stdint.h>

/* Must mirror the pops at the end of arch_context_switch (context.asm). */
struct initial_frame {
    uint64_t r15, r14, r13, r12, rbx, rbp;
    uint64_t entry;          /* popped by `ret` */
    uint64_t return_address; /* what `entry` sees as its caller */
};

void *arch_context_create(void *stack_top, void (*entry)(void))
{
    struct initial_frame *frame = (struct initial_frame *)stack_top - 1;

    *frame = (struct initial_frame){
        /* rbp = 0 ends stack traces of the new thread here. */
        .rbp = 0,
        .entry = (uint64_t)(uintptr_t)entry,
        /* `entry` never returns, but the slot must exist: the ABI promises
         * a function that rsp is 8 below a 16-byte boundary on entry, as it
         * is right after a `call`. */
        .return_address = 0,
    };
    return frame;
}
