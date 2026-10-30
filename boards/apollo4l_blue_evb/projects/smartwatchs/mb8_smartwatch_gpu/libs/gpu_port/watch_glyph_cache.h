#ifndef WATCH_GLYPH_CACHE_H
#define WATCH_GLYPH_CACHE_H
#include <stdint.h>
void watch_gpu_glyph_cache_init(void);
const uint8_t *watch_gpu_glyph_bitmap(const uint8_t *packed, unsigned width,
                                     unsigned height, unsigned bpp);
#endif
