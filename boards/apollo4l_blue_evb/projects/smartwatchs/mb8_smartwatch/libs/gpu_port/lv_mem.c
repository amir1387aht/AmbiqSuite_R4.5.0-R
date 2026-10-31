/* LVGL allocation adapters. The application owns the TLSF pools; lv_init and
 * lv_deinit must never reset heaps containing libc/application/GPU objects. */
#include "lv_mem.h"
#include "lv_gc.h"
#include "lv_assert.h"
#include "lv_log.h"
#include "app_memory.h"
#include <stdint.h>
#define ZERO_MEM_SENTINEL 0xa1b2c3d4u
static uint32_t zero_mem = ZERO_MEM_SENTINEL;
#if LV_LOG_TRACE_MEM
#define MEM_TRACE(...) LV_LOG_TRACE(__VA_ARGS__)
#else
#define MEM_TRACE(...)
#endif
#ifdef LV_ARCH_64
#define ALIGN_MASK 0x7
#else
#define ALIGN_MASK 0x3
#endif
#define COPY32 *d32 = *s32; d32++; s32++;
#define COPY8 *d8 = *s8; d8++; s8++;
#define SET32(x) *d32 = x; d32++;
#define SET8(x) *d8 = x; d8++;
#define REPEAT8(expr) expr expr expr expr expr expr expr expr

void lv_mem_init(void)
{
    app_memory_init(); /* Idempotent; main already powered and initialized RAM. */
    for (uint8_t i = 0; i < LV_MEM_BUF_MAX_NUM; ++i) {
        LV_GC_ROOT(lv_mem_buf[i]).p = NULL;
        LV_GC_ROOT(lv_mem_buf[i]).size = 0;
        LV_GC_ROOT(lv_mem_buf[i]).used = 0;
    }
}
void lv_mem_deinit(void) { lv_mem_buf_free_all(); }
void *lv_mem_alloc(size_t n) { return n ? app_malloc(n) : &zero_mem; }
void *lv_mem_ssram_alloc(size_t n) { return n ? app_malloc_shared(n) : &zero_mem; }
void *lv_mem_external_alloc(size_t n) { return lv_mem_ssram_alloc(n); } /* No PSRAM. */
void lv_mem_free(void *p) { if (p != &zero_mem) app_free(p); }
void *lv_mem_realloc(void *p, size_t n)
{
    if (!n) { lv_mem_free(p); return &zero_mem; }
    return app_realloc(p == &zero_mem ? NULL : p, n);
}
lv_res_t lv_mem_test(void)
{ return zero_mem == ZERO_MEM_SENTINEL && app_memory_check() ? LV_RES_OK : LV_RES_INV; }
static void monitor(lv_mem_monitor_t *m, bool shared_only)
{
    app_memory_pool_stats_t cpu, shared;
    app_memory_stats(&cpu, &shared);
    lv_memset_00(m, sizeof(*m));
    m->total_size = shared.total + (shared_only ? 0 : cpu.total);
    m->free_size = shared.free + (shared_only ? 0 : cpu.free);
    m->free_biggest_size = shared.largest;
    if (!shared_only && cpu.largest > m->free_biggest_size) m->free_biggest_size = cpu.largest;
    m->free_cnt = shared.free_blocks + (shared_only ? 0 : cpu.free_blocks);
    m->used_cnt = shared.used_blocks + (shared_only ? 0 : cpu.used_blocks);
    m->used_pct = 100 - 100u * m->free_size / m->total_size;
    m->frag_pct = m->free_size ? 100 - 100u * m->free_biggest_size / m->free_size : 0;
}
void lv_mem_monitor(lv_mem_monitor_t *m) { monitor(m, false); }
void lv_mem_ssram_monitor(lv_mem_monitor_t *m) { monitor(m, true); }
void lv_mem_external_monitor(lv_mem_monitor_t *m) { monitor(m, true); }
void *lv_mem_font_glyph_alloc(uint32_t n) { return lv_mem_ssram_alloc(n); }
void *lv_mem_font_info_alloc(uint32_t n) { return lv_mem_alloc(n); }

/**
 * Get a temporal buffer with the given size.
 * @param size the required size
 */
void * lv_mem_buf_get(uint32_t size)
{
    if(size == 0) return NULL;

    MEM_TRACE("begin, getting %d bytes", size);

    /*Try to find a free buffer with suitable size*/
    int8_t i_guess = -1;
    for(uint8_t i = 0; i < LV_MEM_BUF_MAX_NUM; i++) {
        if(LV_GC_ROOT(lv_mem_buf[i]).used == 0 && LV_GC_ROOT(lv_mem_buf[i]).size >= size) {
            if(LV_GC_ROOT(lv_mem_buf[i]).size == size) {
                LV_GC_ROOT(lv_mem_buf[i]).used = 1;
                return LV_GC_ROOT(lv_mem_buf[i]).p;
            }
            else if(i_guess < 0) {
                i_guess = i;
            }
            /*If size of `i` is closer to `size` prefer it*/
            else if(LV_GC_ROOT(lv_mem_buf[i]).size < LV_GC_ROOT(lv_mem_buf[i_guess]).size) {
                i_guess = i;
            }
        }
    }

    if(i_guess >= 0) {
        LV_GC_ROOT(lv_mem_buf[i_guess]).used = 1;
        MEM_TRACE("returning already allocated buffer (buffer id: %d, address: %p)", i_guess,
                  LV_GC_ROOT(lv_mem_buf[i_guess]).p);
        return LV_GC_ROOT(lv_mem_buf[i_guess]).p;
    }

    /*Reallocate a free buffer*/
    for(uint8_t i = 0; i < LV_MEM_BUF_MAX_NUM; i++) {
        if(LV_GC_ROOT(lv_mem_buf[i]).used == 0) {
            /*if this fails you probably need to increase your LV_MEM_SIZE/heap size*/
            void * buf = lv_mem_realloc(LV_GC_ROOT(lv_mem_buf[i]).p, size);
            LV_ASSERT_MSG(buf != NULL, "Out of memory, can't allocate a new buffer (increase your LV_MEM_SIZE/heap size)");
            if(buf == NULL) return NULL;

            LV_GC_ROOT(lv_mem_buf[i]).used = 1;
            LV_GC_ROOT(lv_mem_buf[i]).size = size;
            LV_GC_ROOT(lv_mem_buf[i]).p    = buf;
            MEM_TRACE("allocated (buffer id: %d, address: %p)", i, LV_GC_ROOT(lv_mem_buf[i]).p);
            return LV_GC_ROOT(lv_mem_buf[i]).p;
        }
    }

    LV_LOG_ERROR("no more buffers. (increase LV_MEM_BUF_MAX_NUM)");
    LV_ASSERT_MSG(false, "No more buffers. Increase LV_MEM_BUF_MAX_NUM.");
    return NULL;
}

/**
 * Release a memory buffer
 * @param p buffer to release
 */
void lv_mem_buf_release(void * p)
{
    MEM_TRACE("begin (address: %p)", p);

    for(uint8_t i = 0; i < LV_MEM_BUF_MAX_NUM; i++) {
        if(LV_GC_ROOT(lv_mem_buf[i]).p == p) {
            LV_GC_ROOT(lv_mem_buf[i]).used = 0;
            return;
        }
    }

    LV_LOG_ERROR("p is not a known buffer");
}

/**
 * Free all memory buffers
 */
void lv_mem_buf_free_all(void)
{
    for(uint8_t i = 0; i < LV_MEM_BUF_MAX_NUM; i++) {
        if(LV_GC_ROOT(lv_mem_buf[i]).p) {
            lv_mem_free(LV_GC_ROOT(lv_mem_buf[i]).p);
            LV_GC_ROOT(lv_mem_buf[i]).p = NULL;
            LV_GC_ROOT(lv_mem_buf[i]).used = 0;
            LV_GC_ROOT(lv_mem_buf[i]).size = 0;
        }
    }
}

#if LV_MEMCPY_MEMSET_STD == 0
/**
 * Same as `memcpy` but optimized for 4 byte operation.
 * @param dst pointer to the destination buffer
 * @param src pointer to the source buffer
 * @param len number of byte to copy
 */
LV_ATTRIBUTE_FAST_MEM void * lv_memcpy(void * dst, const void * src, size_t len)
{
    uint8_t * d8 = dst;
    const uint8_t * s8 = src;

    lv_uintptr_t d_align = (lv_uintptr_t)d8 & ALIGN_MASK;
    lv_uintptr_t s_align = (lv_uintptr_t)s8 & ALIGN_MASK;

    /*Byte copy for unaligned memories*/
    if(s_align != d_align) {
        while(len > 32) {
            REPEAT8(COPY8);
            REPEAT8(COPY8);
            REPEAT8(COPY8);
            REPEAT8(COPY8);
            len -= 32;
        }
        while(len) {
            COPY8
            len--;
        }
        return dst;
    }

    /*Make the memories aligned*/
    if(d_align) {
        d_align = ALIGN_MASK + 1 - d_align;
        while(d_align && len) {
            COPY8;
            d_align--;
            len--;
        }
    }

    uint32_t * d32 = (uint32_t *)d8;
    const uint32_t * s32 = (uint32_t *)s8;
    while(len > 32) {
        REPEAT8(COPY32)
        len -= 32;
    }

    while(len > 4) {
        COPY32;
        len -= 4;
    }

    d8 = (uint8_t *)d32;
    s8 = (const uint8_t *)s32;
    while(len) {
        COPY8
        len--;
    }

    return dst;
}

/**
 * Same as `memset` but optimized for 4 byte operation.
 * @param dst pointer to the destination buffer
 * @param v value to set [0..255]
 * @param len number of byte to set
 */
LV_ATTRIBUTE_FAST_MEM void lv_memset(void * dst, uint8_t v, size_t len)
{

    uint8_t * d8 = (uint8_t *)dst;

    uintptr_t d_align = (lv_uintptr_t) d8 & ALIGN_MASK;

    /*Make the address aligned*/
    if(d_align) {
        d_align = ALIGN_MASK + 1 - d_align;
        while(d_align && len) {
            SET8(v);
            len--;
            d_align--;
        }
    }

    uint32_t v32 = (uint32_t)v + ((uint32_t)v << 8) + ((uint32_t)v << 16) + ((uint32_t)v << 24);

    uint32_t * d32 = (uint32_t *)d8;

    while(len > 32) {
        REPEAT8(SET32(v32));
        len -= 32;
    }

    while(len > 4) {
        SET32(v32);
        len -= 4;
    }

    d8 = (uint8_t *)d32;
    while(len) {
        SET8(v);
        len--;
    }
}

/**
 * Same as `memset(dst, 0x00, len)` but optimized for 4 byte operation.
 * @param dst pointer to the destination buffer
 * @param len number of byte to set
 */
LV_ATTRIBUTE_FAST_MEM void lv_memset_00(void * dst, size_t len)
{
    uint8_t * d8 = (uint8_t *)dst;
    uintptr_t d_align = (lv_uintptr_t) d8 & ALIGN_MASK;

    /*Make the address aligned*/
    if(d_align) {
        d_align = ALIGN_MASK + 1 - d_align;
        while(d_align && len) {
            SET8(0);
            len--;
            d_align--;
        }
    }

    uint32_t * d32 = (uint32_t *)d8;
    while(len > 32) {
        REPEAT8(SET32(0));
        len -= 32;
    }

    while(len > 4) {
        SET32(0);
        len -= 4;
    }

    d8 = (uint8_t *)d32;
    while(len) {
        SET8(0);
        len--;
    }
}

/**
 * Same as `memset(dst, 0xFF, len)` but optimized for 4 byte operation.
 * @param dst pointer to the destination buffer
 * @param len number of byte to set
 */
LV_ATTRIBUTE_FAST_MEM void lv_memset_ff(void * dst, size_t len)
{
    uint8_t * d8 = (uint8_t *)dst;
    uintptr_t d_align = (lv_uintptr_t) d8 & ALIGN_MASK;

    /*Make the address aligned*/
    if(d_align) {
        d_align = ALIGN_MASK + 1 - d_align;
        while(d_align && len) {
            SET8(0xFF);
            len--;
            d_align--;
        }
    }

    uint32_t * d32 = (uint32_t *)d8;
    while(len > 32) {
        REPEAT8(SET32(0xFFFFFFFF));
        len -= 32;
    }

    while(len > 4) {
        SET32(0xFFFFFFFF);
        len -= 4;
    }

    d8 = (uint8_t *)d32;
    while(len) {
        SET8(0xFF);
        len--;
    }
}

#endif /*LV_MEMCPY_MEMSET_STD*/
