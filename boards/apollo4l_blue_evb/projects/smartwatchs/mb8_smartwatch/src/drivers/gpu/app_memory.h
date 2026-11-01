#ifndef APP_MEMORY_H
#define APP_MEMORY_H
#include <stddef.h>
#include <stdbool.h>

/* Task-context allocator shared by libc, LVGL and Nema. No allocation in ISRs.
 * Ordinary malloc prefers CPU-only TCM, spilling to shared SRAM. Explicit
 * shared allocation is required for GPU/MSPI/IOM buffers; TCM is not visible.
 * Free either kind with free() or app_free(). Pools live for the entire boot. */
typedef struct
{
    size_t total, free, largest, free_blocks, used_blocks;
} app_memory_pool_stats_t;

void  app_memory_init(void); /* AFTER powering RAM, BEFORE tasks or lv_init. */
void *app_malloc(size_t size);
void *app_malloc_shared(size_t size);
void *app_memalign(size_t alignment, size_t size, bool shared_only);
void *app_calloc(size_t count, size_t size);
void *app_realloc(void *ptr, size_t size);
void  app_free(void *ptr);
/* Linear diagnostic walks; task-context only, not a per-frame operation. */
void app_memory_stats(app_memory_pool_stats_t *cpu, app_memory_pool_stats_t *shared);
bool app_memory_check(void);
#endif
