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
    { "thread-churn", test_thread_churn },
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
