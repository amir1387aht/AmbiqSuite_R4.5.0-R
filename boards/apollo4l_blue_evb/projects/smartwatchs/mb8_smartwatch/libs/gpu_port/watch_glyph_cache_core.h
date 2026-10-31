#ifndef WATCH_GLYPH_CACHE_CORE_H
#define WATCH_GLYPH_CACHE_CORE_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>
#define WATCH_GLYPH_CACHE_SLOTS 256u

typedef struct {
    const uint8_t *source;
    uint8_t *gpu_bitmap;
    uint16_t width;
    uint16_t height;
} watch_glyph_entry_t;
typedef struct {
    uint8_t *arena;
    size_t capacity;
    size_t used;
    watch_glyph_entry_t entries[WATCH_GLYPH_CACHE_SLOTS];
} watch_glyph_cache_t;

/* Input: packed, high-nibble-first A4. Output: each row byte-aligned, with an
 * unused low nibble zeroed on odd widths. Source/font descriptors are immutable. */
static inline void watch_a4_align_rows(uint8_t *dst, const uint8_t *src,
                                        unsigned width, unsigned height)
{
    size_t pitch = (width + 1u) / 2u;
    memset(dst, 0, pitch * height);
    for (unsigned y = 0; y < height; ++y) {
        for (unsigned x = 0; x < width; ++x) {
            size_t pixel = (size_t)y * width + x;
            uint8_t a4 = (uint8_t)((src[pixel / 2u] >> ((pixel & 1u) ? 0 : 4)) & 15u);
            dst[(size_t)y * pitch + x / 2u] |= (uint8_t)(a4 << ((x & 1u) ? 0 : 4));
        }
    }
}

static inline void watch_glyph_cache_init(watch_glyph_cache_t *cache,
                                          uint8_t *arena, size_t capacity)
{
    memset(cache, 0, sizeof(*cache));
    cache->arena = arena;
    cache->capacity = capacity;
}

/* No eviction: queued GPU commands may retain pointers until completion.
 * A full cache returns NULL and the caller synchronizes before CPU fallback. */
static inline const uint8_t *watch_glyph_cache_get(watch_glyph_cache_t *cache,
    const uint8_t *source, unsigned width, unsigned height, bool *fresh)
{
    *fresh = false;
    if (!cache->arena || !source || !width || !height || width > 1024u || height > 1024u)
        return NULL;
    uintptr_t hash = ((uintptr_t)source >> 2u) ^ ((uintptr_t)width * 37u) ^ height;
    for (unsigned probe = 0; probe < WATCH_GLYPH_CACHE_SLOTS; ++probe) {
        watch_glyph_entry_t *entry = &cache->entries[(hash + probe) & (WATCH_GLYPH_CACHE_SLOTS - 1u)];
        if (entry->source == source && entry->width == width && entry->height == height)
            return entry->gpu_bitmap;
        if (!entry->source) {
            size_t bytes = ((width + 1u) / 2u) * height;
            size_t offset = (cache->used + 7u) & ~(size_t)7u;
            if (offset > cache->capacity || bytes > cache->capacity - offset) return NULL;
            uint8_t *dest = cache->arena + offset;
            watch_a4_align_rows(dest, source, width, height);
            entry->source = source;
            entry->gpu_bitmap = dest;
            entry->width = (uint16_t)width;
            entry->height = (uint16_t)height;
            cache->used = offset + bytes;
            *fresh = true;
            return dest;
        }
    }
    return NULL;
}
#endif
