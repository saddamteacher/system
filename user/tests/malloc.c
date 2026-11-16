/* The C library heap: correctness under a randomised workload. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define SLOTS 48
#define ROUNDS 3000

#define CHECK(condition) \
    do { \
        if (!(condition)) { \
            printf("malloc: line %d: failed: %s\n", __LINE__, #condition); \
            return 1; \
        } \
    } while (0)

static unsigned next_random(unsigned *state)
{
    *state = *state * 1664525u + 1013904223u;
    return *state >> 8;
}

static int filled_with(const unsigned char *block, size_t size, unsigned char value)
{
    for (size_t i = 0; i < size; i++) {
        if (block[i] != value) {
            return 0;
        }
    }
    return 1;
}

int main(void)
{
    static unsigned char *slots[SLOTS];
    static size_t sizes[SLOTS];
    unsigned random = 2024;

    CHECK(malloc(0) == NULL);

    unsigned char *zeroed = calloc(100, 8);
    CHECK(zeroed != NULL && filled_with(zeroed, 800, 0));
    CHECK((unsigned long)zeroed % 16 == 0);

    /* realloc keeps the contents. */
    memset(zeroed, 0x5A, 800);
    zeroed = realloc(zeroed, 200000);
    CHECK(zeroed != NULL && filled_with(zeroed, 800, 0x5A));
    free(zeroed);

    for (int round = 0; round < ROUNDS; round++) {
        unsigned slot = next_random(&random) % SLOTS;
        if (slots[slot] != NULL) {
            /* A block overwritten by a neighbour would fail this. */
            CHECK(filled_with(slots[slot], sizes[slot], (unsigned char)slot));
            free(slots[slot]);
            slots[slot] = NULL;
            continue;
        }
        size_t size = 1 + next_random(&random) % 3000;
        if (next_random(&random) % 50 == 0) {
            size = 300 * 1024; /* larger than an arena */
        }
        slots[slot] = malloc(size);
        CHECK(slots[slot] != NULL);
        sizes[slot] = size;
        memset(slots[slot], (int)slot, size);
    }
    for (unsigned slot = 0; slot < SLOTS; slot++) {
        if (slots[slot] != NULL) {
            CHECK(filled_with(slots[slot], sizes[slot], (unsigned char)slot));
            free(slots[slot]);
        }
    }
    printf("malloc: all checks passed\n");
    return 0;
}
