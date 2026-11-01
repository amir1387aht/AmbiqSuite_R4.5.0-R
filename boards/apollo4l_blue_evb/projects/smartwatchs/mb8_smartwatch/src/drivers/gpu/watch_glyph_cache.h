#ifndef WATCH_GLYPH_CACHE_H
#define WATCH_GLYPH_CACHE_H
#include <stdint.h>
#include <stddef.h>
void           watch_gpu_glyph_cache_init(void);
const uint8_t *watch_gpu_glyph_bitmap(
    const uint8_t *packed, unsigned width, unsigned height, unsigned bpp);

size_t watch_gpu_glyph_cache_capacity(void);
size_t watch_gpu_glyph_cache_used(void);
#endif
