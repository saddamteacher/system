/*
 * State: `user_fault_handler` is set once at boot. `panicking` is set by the first panic and never cleared; it exists
 * to detect a panic that happens while another panic is being reported.
 */
#include <core/panic.h>

#include <stdatomic.h>

#include <arch/cpu.h>
#include <arch/trap.h>
#include <core/log.h>

#define STACK_TRACE_MAX_DEPTH 32
#define REGISTERS_PER_LINE 3

static atomic_flag panicking = ATOMIC_FLAG_INIT;
/* Written once during boot, before any user code runs. */
static user_fault_handler_fn user_fault_handler;

void panic_set_user_fault_handler(user_fault_handler_fn handler)
{
    user_fault_handler = handler;
}

static void panic_begin(void)
{
    /* Nothing may preempt the report; interrupts stay off for good. */
    (void)arch_irq_save();
    log_enter_panic_mode();

    if (atomic_flag_test_and_set(&panicking)) {
        /* The panic code itself faulted (most likely while walking a broken
         * stack). Reporting again would loop, so stop with a short note. */
        kprintf("\nPANIC: nested panic, giving up\n");
        arch_debug_exit(ARCH_DEBUG_EXIT_FAILURE);
        arch_halt_forever();
    }
}

_Noreturn static void panic_end(void)
{
    kprintf("System halted.\n");
    arch_debug_exit(ARCH_DEBUG_EXIT_FAILURE);
    arch_halt_forever();
}

/*
 * Prints return addresses, innermost first. Turn them into function names
 * with `make symbolize ADDR=0x...`.
 */
static void print_stack_trace(uintptr_t frame_pointer)
{
    kprintf("Stack trace:\n");
    for (int depth = 0; depth < STACK_TRACE_MAX_DEPTH; depth++) {
        uintptr_t return_address;
        if (!arch_stack_walk(&frame_pointer, &return_address)) {
            return;
        }
        kprintf("  [%d] %p\n", depth, (void *)return_address);
    }
    kprintf("  ... (truncated)\n");
}

static void print_registers(const struct arch_trap_frame *frame)
{
    struct arch_register registers[ARCH_TRAP_REGISTER_MAX];
    size_t count = arch_trap_registers(frame, registers, ARCH_TRAP_REGISTER_MAX);

    for (size_t i = 0; i < count; i++) {
        kprintf("  %6s=%016llx", registers[i].name, (unsigned long long)registers[i].value);
        if ((i + 1) % REGISTERS_PER_LINE == 0 || i + 1 == count) {
            kprintf("\n");
        }
    }
}

_Noreturn void panic(const char *format, ...)
{
    panic_begin();

    va_list arguments;
    va_start(arguments, format);
    kprintf("\nPANIC: ");
    kvprintf(format, arguments);
    kprintf("\n");
    va_end(arguments);

    print_stack_trace((uintptr_t)__builtin_frame_address(0));
    panic_end();
}

_Noreturn static void panic_on_trap(const struct arch_trap_frame *frame, const struct arch_trap_info *info)
{
    panic_begin();

    kprintf("\nPANIC: CPU exception: %s\n", info->name);
    kprintf("  vector=%llu error_code=0x%llx\n", (unsigned long long)info->vector,
            (unsigned long long)info->error_code);
    if (info->has_fault_address) {
        kprintf("  fault address=%p\n", (void *)info->fault_address);
    }
    kprintf("Registers:\n");
    print_registers(frame);

    /* The trace starts in the code that trapped, not in this handler. */
    kprintf("Faulting instruction: %p\n", (void *)arch_trap_pc(frame));
    print_stack_trace(arch_trap_frame_pointer(frame));
    panic_end();
}

void kernel_handle_trap(const struct arch_trap_frame *frame, const struct arch_trap_info *info)
{
    if (info->kind == ARCH_TRAP_BREAKPOINT) {
        log_info("trap: breakpoint at %p", (void *)arch_trap_pc(frame));
        return;
    }
    if (info->from_user && user_fault_handler != NULL) {
        user_fault_handler(frame, info);
    }
    panic_on_trap(frame, info);
}
