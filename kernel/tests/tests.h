#ifndef TESTS_TESTS_H
#define TESTS_TESTS_H

#include <stdbool.h>

/*
 * In-kernel self-tests, selected at boot with `test=<name>` on the kernel
 * command line (see `make check`). Some tests are expected to crash the
 * kernel on purpose; those never return.
 */

enum test_result { TEST_PASSED, TEST_FAILED, TEST_UNKNOWN };

enum test_result tests_run(const char *name);

#endif
