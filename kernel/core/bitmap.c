#include <core/bitmap.h>

#define WORD_FULL (~0ULL)

size_t bitmap_storage_size(size_t bit_count)
{
    size_t words = (bit_count + BITMAP_BITS_PER_WORD - 1) / BITMAP_BITS_PER_WORD;
    return words * sizeof(uint64_t);
}

void bitmap_init(struct bitmap *bitmap, uint64_t *words, size_t bit_count)
{
    bitmap->words = words;
    bitmap->bit_count = bit_count;
}

bool bitmap_test(const struct bitmap *bitmap, size_t index)
{
    return (bitmap->words[index / BITMAP_BITS_PER_WORD] >> (index % BITMAP_BITS_PER_WORD)) & 1;
}

void bitmap_set_range(struct bitmap *bitmap, size_t start, size_t count)
{
    for (size_t i = start; i < start + count; i++) {
        bitmap->words[i / BITMAP_BITS_PER_WORD] |= 1ULL << (i % BITMAP_BITS_PER_WORD);
    }
}

void bitmap_clear_range(struct bitmap *bitmap, size_t start, size_t count)
{
    for (size_t i = start; i < start + count; i++) {
        bitmap->words[i / BITMAP_BITS_PER_WORD] &= ~(1ULL << (i % BITMAP_BITS_PER_WORD));
    }
}

bool bitmap_all_set(const struct bitmap *bitmap, size_t start, size_t count)
{
    for (size_t i = start; i < start + count; i++) {
        if (!bitmap_test(bitmap, i)) {
            return false;
        }
    }
    return true;
}

/* Searches [from, to) for a run of `count` clear bits. */
static size_t find_clear_run_in(const struct bitmap *bitmap, size_t from, size_t to, size_t count)
{
    size_t run = 0;

    for (size_t i = from; i < to; i++) {
        bool at_word_start = (i % BITMAP_BITS_PER_WORD) == 0;

        /* Skip 64 used bits at once; this is what makes scanning a mostly
         * full bitmap cheap. */
        if (at_word_start && i + BITMAP_BITS_PER_WORD <= to
            && bitmap->words[i / BITMAP_BITS_PER_WORD] == WORD_FULL) {
            run = 0;
            i += BITMAP_BITS_PER_WORD - 1;
            continue;
        }
        if (bitmap_test(bitmap, i)) {
            run = 0;
        } else if (++run == count) {
            return i + 1 - count;
        }
    }
    return BITMAP_NOT_FOUND;
}

size_t bitmap_find_clear_run(const struct bitmap *bitmap, size_t hint, size_t count)
{
    if (count == 0 || count > bitmap->bit_count) {
        return BITMAP_NOT_FOUND;
    }
    if (hint >= bitmap->bit_count) {
        hint = 0;
    }
    size_t found = find_clear_run_in(bitmap, hint, bitmap->bit_count, count);
    if (found == BITMAP_NOT_FOUND && hint != 0) {
        found = find_clear_run_in(bitmap, 0, bitmap->bit_count, count);
    }
    return found;
}

size_t bitmap_count_clear(const struct bitmap *bitmap)
{
    size_t clear = 0;
    for (size_t i = 0; i < bitmap->bit_count; i++) {
        if (!bitmap_test(bitmap, i)) {
            clear++;
        }
    }
    return clear;
}
