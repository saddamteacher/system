#ifndef GUI_ROWOPS_INTERNAL_H
#define GUI_ROWOPS_INTERNAL_H

#include <stdint.h>

/* One implementation of the row operations. */
struct rowops {
    const char *name;
    void (*fill)(uint32_t *row, int count, uint32_t pixel);
    void (*blend)(uint32_t *row, const uint32_t *source, int count);
    void (*blend_opacity)(uint32_t *row, const uint32_t *source, int count, uint32_t opacity);
};

extern const struct rowops rowops_portable;
/* Provided by the architecture directory; NULL where there is none. */
extern const struct rowops *const rowops_accelerated;

#endif
