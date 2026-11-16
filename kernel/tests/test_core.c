/* Tests for kernel/core. */
#include "tests_internal.h"

#include <stddef.h>

#include <core/bitmap.h>
#include <core/cmdline.h>
#include <core/list.h>
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

bool test_printf(void)
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

bool test_cmdline(void)
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


bool test_bitmap(void)
{
    uint64_t words[3];
    struct bitmap bitmap;
    bitmap_init(&bitmap, words, 150);
    bitmap_clear_range(&bitmap, 0, 150);

    bool passed = true;
    passed &= bitmap_count_clear(&bitmap) == 150;
    passed &= bitmap_find_clear_run(&bitmap, 0, 150) == 0;
    passed &= bitmap_find_clear_run(&bitmap, 0, 151) == BITMAP_NOT_FOUND;

    /* Used: [0,70) and [75,150). The only gap is [70,75). */
    bitmap_set_range(&bitmap, 0, 70);
    bitmap_set_range(&bitmap, 75, 75);
    passed &= bitmap_test(&bitmap, 69) && !bitmap_test(&bitmap, 70) && bitmap_test(&bitmap, 75);
    passed &= bitmap_find_clear_run(&bitmap, 0, 5) == 70;
    passed &= bitmap_find_clear_run(&bitmap, 0, 6) == BITMAP_NOT_FOUND;
    /* A hint past the gap must wrap around and still find it. */
    passed &= bitmap_find_clear_run(&bitmap, 100, 3) == 70;
    passed &= bitmap_all_set(&bitmap, 0, 70) && !bitmap_all_set(&bitmap, 69, 2);
    passed &= bitmap_count_clear(&bitmap) == 5;

    if (!passed) {
        log_error("bitmap test: an expectation failed");
    }
    return passed;
}

struct numbered {
    int value;
    struct list_node link;
};

bool test_list(void)
{
    struct list_node head;
    struct numbered items[3] = { { .value = 1 }, { .value = 2 }, { .value = 3 } };

    list_init(&head);
    bool passed = list_is_empty(&head);

    list_push_back(&head, &items[1].link);
    list_push_back(&head, &items[2].link);
    list_push_front(&head, &items[0].link);

    int expected = 1;
    list_for_each (node, &head) {
        passed &= container_of(node, struct numbered, link)->value == expected++;
    }
    passed &= expected == 4;

    list_remove(&items[1].link);
    passed &= head.next == &items[0].link && items[0].link.next == &items[2].link;
    list_remove(&items[0].link);
    list_remove(&items[2].link);
    passed &= list_is_empty(&head);

    if (!passed) {
        log_error("list test: an expectation failed");
    }
    return passed;
}
