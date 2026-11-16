#ifndef CORE_BITMAP_H
#define CORE_BITMAP_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/*
 * Fixed-size bit set over caller-provided storage. Used by the allocators
 * that hand out page-sized units: one bit per unit, set = in use.
 *
 * Locking is the caller's responsibility.
 */
struct bitmap {
    uint64_t *words;
    size_t bit_count;
};

#define BITMAP_BITS_PER_WORD 64
#define BITMAP_NOT_FOUND ((size_t)-1)

/* Bytes of storage needed for `bit_count` bits. */
size_t bitmap_storage_size(size_t bit_count);

void bitmap_init(struct bitmap *bitmap, uint64_t *words, size_t bit_count);

bool bitmap_test(const struct bitmap *bitmap, size_t index);
void bitmap_set_range(struct bitmap *bitmap, size_t start, size_t count);
void bitmap_clear_range(struct bitmap *bitmap, size_t start, size_t count);

/* True if every bit in [start, start + count) is set. */
bool bitmap_all_set(const struct bitmap *bitmap, size_t start, size_t count);

/*
 * Finds `count` consecutive clear bits. The search starts at `hint` and
 * wraps around once, so repeated allocations do not rescan the same full
 * prefix every time. Returns the first index or BITMAP_NOT_FOUND.
 */
size_t bitmap_find_clear_run(const struct bitmap *bitmap, size_t hint, size_t count);

size_t bitmap_count_clear(const struct bitmap *bitmap);

#endif
