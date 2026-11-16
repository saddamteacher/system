#ifndef TESTS_TESTS_INTERNAL_H
#define TESTS_TESTS_INTERNAL_H

#include <stdbool.h>
#include <stdint.h>

/* Each test returns true on success. Tests named in the "crash" group are
 * expected to panic and never return. */

/* test_core.c */
bool test_printf(void);
bool test_cmdline(void);
bool test_bitmap(void);
bool test_list(void);

/* test_mm.c */
bool test_pmm(void);
bool test_vmm(void);
bool test_heap(void);

/* test_devices.c */
bool test_timer(void);
bool test_keyboard(void);

/* test_sched.c */
bool test_threads(void);
bool test_sleep(void);
bool test_mutex(void);
bool test_thread_churn(void);

/* test_user.c */
bool test_boot(void);
bool test_user_hello(void);
bool test_user_syscalls(void);
bool test_user_files(void);
bool test_user_malloc(void);
bool test_user_spawn(void);
bool test_user_fpu(void);
bool test_user_sleep(void);
bool test_user_fault(void);
bool test_user_churn(void);

/* test_traps.c */
bool test_breakpoint(void);
bool test_divide_by_zero(void);
bool test_page_fault(void);
bool test_write_rodata(void);
bool test_use_after_unmap(void);
bool test_stack_overflow(void);

#endif
