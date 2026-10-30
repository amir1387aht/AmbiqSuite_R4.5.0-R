#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include "watch_glyph_cache_core.h"
static _Alignas(8) uint8_t arena[96 * 1024];
static watch_glyph_cache_t cache;
static uint8_t sources[300][128];

int main(void)
{
    uint8_t packed[(65 * 65 + 1) / 2];
    uint8_t output[((65 + 1) / 2) * 65 + 16];
    unsigned cases = 0;
    for (unsigned w = 1; w <= 65; ++w) {
        for (unsigned h = 1; h <= 65; ++h) {
            size_t input_bytes = (w * h + 1u) / 2u;
            for (size_t i = 0; i < input_bytes; ++i) packed[i] = (uint8_t)(i * 71u + w + h);
            memset(output, 0xa5, sizeof(output));
            watch_a4_align_rows(output + 8, packed, w, h);
            unsigned pitch = (w + 1u) / 2u;
            for (unsigned y = 0; y < h; ++y) {
                for (unsigned x = 0; x < w; ++x) {
                    unsigned i = y * w + x;
                    unsigned expected = (packed[i / 2] >> ((i & 1) ? 0 : 4)) & 15u;
                    unsigned got = (output[8 + y * pitch + x / 2] >> ((x & 1) ? 0 : 4)) & 15u;
                    assert(got == expected);
                }
                if (w & 1) assert((output[8 + y * pitch + pitch - 1] & 15u) == 0);
            }
            for (unsigned i = 0; i < 8; ++i) assert(output[i] == 0xa5);
            for (size_t i = 8 + pitch * h; i < sizeof(output); ++i) assert(output[i] == 0xa5);
            ++cases;
        }
    }
    for (unsigned i = 0; i < 300; ++i)
        for (unsigned j = 0; j < 128; ++j) sources[i][j] = (uint8_t)(i + j);
    watch_glyph_cache_init(&cache, arena, sizeof(arena));
    bool fresh;
    const uint8_t *first = watch_glyph_cache_get(&cache, sources[0], 8, 8, &fresh);
    assert(first && fresh && ((uintptr_t)first & 7u) == 0);
    size_t used = cache.used;
    assert(watch_glyph_cache_get(&cache, sources[0], 8, 8, &fresh) == first && !fresh);
    assert(cache.used == used);
    for (unsigned i = 1; i < WATCH_GLYPH_CACHE_SLOTS; ++i) {
        const uint8_t *p = watch_glyph_cache_get(&cache, sources[i], 8, 8, &fresh);
        assert(p && fresh && ((uintptr_t)p & 7u) == 0);
    }
    assert(!watch_glyph_cache_get(&cache, sources[256], 8, 8, &fresh));
    assert(watch_glyph_cache_get(&cache, sources[0], 8, 8, &fresh) == first && !fresh);
    assert(memcmp(first, sources[0], 32) == 0);
    watch_glyph_cache_init(&cache, arena, 16);
    assert(!watch_glyph_cache_get(&cache, sources[0], 9, 9, &fresh));
    assert(cache.used == 0);
    first = watch_glyph_cache_get(&cache, sources[0], 2, 2, &fresh);
    assert(first && fresh);
    assert(!watch_glyph_cache_get(&cache, sources[1], 1025, 1, &fresh));
    assert(!watch_glyph_cache_get(&cache, sources[1], 0, 1, &fresh));
    assert(!watch_glyph_cache_get(&cache, NULL, 1, 1, &fresh));
    assert(watch_glyph_cache_get(&cache, sources[0], 2, 2, &fresh) == first);
    printf("PASS: %u A4 layouts; odd-width padding/guards; cache reuse, alignment, table full and arena exhaustion without eviction\n", cases);
    return 0;
}
