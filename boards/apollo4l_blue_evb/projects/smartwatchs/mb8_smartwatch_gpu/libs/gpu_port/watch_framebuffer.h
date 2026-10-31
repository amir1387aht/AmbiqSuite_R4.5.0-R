#ifndef WATCH_FRAMEBUFFER_H
#define WATCH_FRAMEBUFFER_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include "watch_rgb565.h"


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

/* Copy a packed BGRA8888 LVGL rectangle into a strided persistent RGB565 frame.
 * dst points at the rectangle's top-left pixel; untouched pixels are retained.
 * memcpy avoids aliasing lv_color_t unions through a uint32_t array pointer. */
static inline void watch_bgra8888_rect_to_rgb565(uint16_t *dst, size_t dst_stride,
                                                const void *src_pixels,
                                                size_t width, size_t height)
{
    const uint8_t *src = (const uint8_t *)src_pixels;
    for (size_t y = 0; y < height; ++y) {
        for (size_t x = 0; x < width; ++x) {
            uint32_t pixel;
            memcpy(&pixel, src + x * sizeof(pixel), sizeof(pixel));
            dst[x] = watch_bgra8888_to_rgb565(pixel);
        }
        src += width * sizeof(uint32_t);
        dst += dst_stride;
    }
}
#endif
