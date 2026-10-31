#ifndef WATCH_FRAMEBUFFER_H
#define WATCH_FRAMEBUFFER_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>


typedef struct {
    uint32_t previous_te;
    bool seen;
} watch_te_cadence_t;

/* Number of TE periods since the preceding submitted frame, saturated at 3.
 * Zero is the initial sample (or an impossible same-edge repeat). */
static inline unsigned watch_te_cadence_sample(watch_te_cadence_t *state, uint32_t te)
{
    uint32_t gap = te - state->previous_te;
    unsigned result = state->seen ? (gap > 2u ? 3u : (unsigned)gap) : 0u;
    state->previous_te = te;
    state->seen = true;
    return result;
}

typedef struct {
    unsigned first;
    unsigned last;
    bool valid;
} watch_dirty_rows_t;

static inline void watch_dirty_rows_include(watch_dirty_rows_t *rows,
                                            unsigned first, unsigned last)
{
    if (!rows->valid || first < rows->first) rows->first = first;
    if (!rows->valid || last > rows->last) rows->last = last;
    rows->valid = true;
}

#endif
