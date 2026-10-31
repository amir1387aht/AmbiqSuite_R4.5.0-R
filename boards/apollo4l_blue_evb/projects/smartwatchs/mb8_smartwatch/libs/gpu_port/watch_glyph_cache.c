#include "watch_glyph_cache.h"
#include "watch_glyph_cache_core.h"
#include "watch_gpu.h"
#include "app_memory.h"
#include "lvgl.h"
#include "am_mcu_apollo.h"
#include "am_util.h"
#define WATCH_GLYPH_ARENA_BYTES (96u * 1024u)
static watch_glyph_cache_t glyph_cache;

void watch_gpu_glyph_cache_init(void)
{
    uint8_t *arena = app_malloc_shared(WATCH_GLYPH_ARENA_BYTES);
    watch_glyph_cache_init(&glyph_cache, arena, arena ? WATCH_GLYPH_ARENA_BYTES : 0);
    am_util_stdio_printf(
        "Nema A4 glyph cache: %lu bytes in shared SRAM\r\n", (unsigned long)glyph_cache.capacity);
}

const uint8_t *watch_gpu_glyph_bitmap(
    const uint8_t *packed, unsigned width, unsigned height, unsigned bpp)
{
    /* Only immutable MRAM bitmaps are keyed by source pointer. Compressed,
     * transient or user-modifiable glyph buffers must keep the CPU path. */
    if (!width || !height || width > 1024u || height > 1024u || bpp != 4)
        return NULL;
    uintptr_t start        = (uintptr_t)packed;
    size_t    packed_bytes = ((size_t)width * height + 1u) / 2u;
    if (bpp != 4 || start < 0x00018000u || start >= 0x00200000u ||
        packed_bytes > 0x00200000u - start)
        return NULL;
    bool           fresh;
    const uint8_t *result = watch_glyph_cache_get(&glyph_cache, packed, width, height, &fresh);
    if (result)
    {
        if (fresh)
        {
            ++watch_gpu_stats.glyph_cache_new;
            /* CPU wrote A4 data that will be read by the GPU. */
            __DSB();
            am_hal_daxi_control(AM_HAL_DAXI_CONTROL_FLUSH, NULL);
        }
        else
        {
            ++watch_gpu_stats.glyph_cache_hits;
        }
    }
    return result;
}

size_t watch_gpu_glyph_cache_capacity(void)
{
    return glyph_cache.capacity;
}

size_t watch_gpu_glyph_cache_used(void)
{
    return glyph_cache.used;
}
