#ifndef WATCH_NEMA_MEMORY_H
#define WATCH_NEMA_MEMORY_H
#include <stdint.h>
#include <stddef.h>
#include "app_memory.h"
#include "nema_sys_defs.h"

/* Framebuffers, textures, ring buffers and command lists MUST be GPU-visible.
 * Miscellaneous host metadata may use TCM. Never send a TCM pointer to DMA. */
static inline void *watch_nema_malloc(int pool, size_t bytes)
{
    switch (pool)
    {
        case NEMA_MEM_POOL_CL_RB:
        case NEMA_MEM_POOL_FB_TEX:
            return app_malloc_shared(bytes);
        case NEMA_MEM_POOL_MISC:
            return app_malloc(bytes);
        default:
            return NULL;
    }
}

#define NEMA_CUSTOM_MALLOC(pool, size) watch_nema_malloc(pool, size)
#define NEMA_CUSTOM_FREE(pointer)      app_free(pointer)
#endif
