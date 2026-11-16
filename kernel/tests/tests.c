#include <tests/tests.h>

#include <stddef.h>

#include <core/string.h>

#include "tests_internal.h"

struct test_case {
    const char *name;
    bool (*run)(void);
};

static const struct test_case test_cases[] = {
    { "printf", test_printf },
    { "cmdline", test_cmdline },
    { "bitmap", test_bitmap },
    { "list", test_list },
    { "pmm", test_pmm },
    { "vmm", test_vmm },
    { "heap", test_heap },
    { "timer", test_timer },
    { "keyboard", test_keyboard },
    { "threads", test_threads },
    { "sleep", test_sleep },
    { "mutex", test_mutex },
    { "timed-wait", test_timed_wait },
    { "thread-churn", test_thread_churn },
    { "boot", test_boot },
    { "user-hello", test_user_hello },
    { "user-syscalls", test_user_syscalls },
    { "user-files", test_user_files },
    { "user-malloc", test_user_malloc },
    { "user-spawn", test_user_spawn },
    { "user-fpu", test_user_fpu },
    { "user-sleep", test_user_sleep },
    { "user-fault", test_user_fault },
    { "user-churn", test_user_churn },
    { "user-ipc", test_user_ipc },
    { "user-ipc-edge", test_user_ipc_edge },
    { "user-gfx", test_user_gfx },
    { "breakpoint", test_breakpoint },
    { "divide-by-zero", test_divide_by_zero },
    { "page-fault", test_page_fault },
    { "write-rodata", test_write_rodata },
    { "use-after-unmap", test_use_after_unmap },
    { "stack-overflow", test_stack_overflow },
};

enum test_result tests_run(const char *name)
{
    for (size_t i = 0; i < sizeof(test_cases) / sizeof(test_cases[0]); i++) {
        if (strcmp(test_cases[i].name, name) == 0) {
            return test_cases[i].run() ? TEST_PASSED : TEST_FAILED;
        }
    }
    return TEST_UNKNOWN;
}
