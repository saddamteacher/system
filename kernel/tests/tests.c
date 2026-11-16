#include <tests/tests.h>

#include <stddef.h>

#include <arch/cpu.h>
#include <core/cmdline.h>
#include <core/log.h>
#include <core/printf.h>
#include <core/string.h>

/* Formats with ksnprintf and compares against the expected text. */
#define EXPECT_FORMAT(expected, ...) \
    do { \
        char actual[64]; \
        ksnprintf(actual, sizeof(actual), __VA_ARGS__); \
        if (strcmp(actual, (expected)) != 0) { \
            log_error("printf test: expected \"%s\", got \"%s\"", (expected), actual); \
            passed = false; \
        } \
    } while (0)

static bool test_printf(void)
{
    bool passed = true;

    EXPECT_FORMAT("plain", "plain");
    EXPECT_FORMAT("a b", "%c %c", 'a', 'b');
    EXPECT_FORMAT("text (null)", "%s %s", "text", (const char *)NULL);
    EXPECT_FORMAT("0 42 -42", "%d %d %i", 0, 42, -42);
    EXPECT_FORMAT("-2147483648", "%d", -2147483647 - 1);
    EXPECT_FORMAT("4294967295", "%u", 4294967295u);
    EXPECT_FORMAT("ff FF 0", "%x %X %x", 255, 255, 0);
    EXPECT_FORMAT("-9223372036854775808", "%lld", -9223372036854775807LL - 1);
    EXPECT_FORMAT("18446744073709551615", "%llu", 18446744073709551615ULL);
    EXPECT_FORMAT("deadbeefcafe", "%lx", 0xdeadbeefcafeUL);
    EXPECT_FORMAT("123", "%zu", (size_t)123);
    EXPECT_FORMAT("[   42]", "[%5d]", 42);
    EXPECT_FORMAT("[42   ]", "[%-5d]", 42);
    EXPECT_FORMAT("[00042]", "[%05d]", 42);
    EXPECT_FORMAT("[-0042]", "[%05d]", -42);
    EXPECT_FORMAT("[  -42]", "[%5d]", -42);
    EXPECT_FORMAT("[   ab]", "[%5s]", "ab");
    EXPECT_FORMAT("[ab   ]", "[%-5s]", "ab");
    EXPECT_FORMAT("0x00000000deadbeef", "%p", (void *)0xdeadbeefUL);
    EXPECT_FORMAT("100%", "100%%");

    /* Truncation: output is cut but the return value is the full length. */
    char small[4];
    int length = ksnprintf(small, sizeof(small), "%s", "abcdef");
    if (length != 6 || strcmp(small, "abc") != 0) {
        log_error("printf test: truncation gave length %d, text \"%s\"", length, small);
        passed = false;
    }
    return passed;
}

static bool expect_cmdline(const char *cmdline, const char *key, const char *expected)
{
    char value[8];
    bool found = cmdline_get(cmdline, key, value, sizeof(value));

    if (expected == NULL ? !found : (found && strcmp(value, expected) == 0)) {
        return true;
    }
    log_error("cmdline test: key \"%s\" in \"%s\" gave \"%s\"", key, cmdline, found ? value : "(not found)");
    return false;
}

static bool test_cmdline(void)
{
    bool passed = true;

    passed &= expect_cmdline("test=abc", "test", "abc");
    passed &= expect_cmdline("  a=1   test=abc  b=2 ", "test", "abc");
    passed &= expect_cmdline("a=1 b=2", "b", "2");
    passed &= expect_cmdline("test=", "test", "");
    passed &= expect_cmdline("", "test", NULL);
    passed &= expect_cmdline("testing=1", "test", NULL);
    passed &= expect_cmdline("test", "test", NULL);
    passed &= expect_cmdline("test=0123456789", "test", "0123456"); /* truncated to fit */
    return passed;
}

static bool test_breakpoint(void)
{
    /* Exercises the full round trip through the interrupt entry code: if any
     * register or the stack were not restored exactly, we would not get back
     * here with `marker` intact. */
    volatile unsigned long marker = 0x1122334455667788UL;
    arch_breakpoint();
    return marker == 0x1122334455667788UL;
}

static bool test_divide_by_zero(void)
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

static bool test_page_fault(void)
{
    /* An address in the lower half that nothing maps. volatile so the
     * compiler performs the read instead of reasoning about it. */
    volatile unsigned char *volatile unmapped = (volatile unsigned char *)0xdead0000UL;
    volatile unsigned char value = *unmapped;

    (void)value;
    return false;
}

struct test_case {
    const char *name;
    bool (*run)(void);
};

static const struct test_case test_cases[] = {
    { "printf", test_printf },
    { "cmdline", test_cmdline },
    { "breakpoint", test_breakpoint },
    { "divide-by-zero", test_divide_by_zero },
    { "page-fault", test_page_fault },
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
