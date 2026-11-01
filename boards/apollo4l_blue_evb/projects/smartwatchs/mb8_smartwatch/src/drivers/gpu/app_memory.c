/* One application allocator, two hardware capabilities. TLSF metadata changes
 * are O(1) and briefly IRQ-masked; memset/memcpy and diagnostic walks are NOT.
 * FreeRTOS keeps its existing dedicated heap_4 for kernel objects/task stacks. */
#include "app_memory.h"
#include "lv_tlsf.h"
#include "am_mcu_apollo.h"
#include "FreeRTOS.h"
#include "task.h"
#include <stdint.h>
#include <string.h>
#include <errno.h>

extern unsigned char __app_cpu_heap_start__, __app_cpu_heap_end__;
extern unsigned char __app_shared_heap_start__, __app_shared_heap_end__;
static lv_tlsf_t     cpu_heap, shared_heap;

static uint32_t lock_heap(void)
{
    configASSERT(__get_IPSR() == 0); /* No malloc/free from an ISR. */
    uint32_t interrupt_mask = __get_PRIMASK();
    __disable_irq();
    __DMB();

    return interrupt_mask;
}

static void unlock_heap(uint32_t interrupt_mask)
{
    __DMB();
    __set_PRIMASK(interrupt_mask);
}

void app_memory_init(void)
{
    uint32_t interrupt_mask = lock_heap();

    if (!cpu_heap)
    {
        cpu_heap    = lv_tlsf_create_with_pool(&__app_cpu_heap_start__,
            (uintptr_t)&__app_cpu_heap_end__ - (uintptr_t)&__app_cpu_heap_start__);
        shared_heap = lv_tlsf_create_with_pool(&__app_shared_heap_start__,
            (uintptr_t)&__app_shared_heap_end__ - (uintptr_t)&__app_shared_heap_start__);
        configASSERT(cpu_heap && shared_heap);
    }
    unlock_heap(interrupt_mask);
}

static lv_tlsf_t heap_for_pointer(void *ptr)
{
    uintptr_t p = (uintptr_t)ptr;
    if (p >= (uintptr_t)&__app_cpu_heap_start__ && p < (uintptr_t)&__app_cpu_heap_end__)
        return cpu_heap;
    if (p >= (uintptr_t)&__app_shared_heap_start__ && p < (uintptr_t)&__app_shared_heap_end__)
        return shared_heap;
    configASSERT(0); /* Wrong allocator or corrupt pointer, never guess a pool. */
    return NULL;
}

void *app_memalign(size_t alignment, size_t size, bool shared_only)
{
    if (!alignment || (alignment & (alignment - 1)))
    {
        errno = EINVAL;
        return NULL;
    }
    if (alignment < _Alignof(max_align_t))
        alignment = _Alignof(max_align_t);

    /* Bound before TLSF's internal align-up arithmetic can overflow. Each
     * hardware bank is smaller than TLSF's configured maximum block size. */
    if (!size)
        size = 1;

    size_t maximum = lv_tlsf_block_size_max();
    if (size >= maximum || alignment >= maximum || size > maximum - alignment - 64u)
    {
        errno = ENOMEM;
        return NULL;
    }
    uint32_t interrupt_mask = lock_heap();
    configASSERT(cpu_heap && shared_heap); /* Explicit early boot initialization. */

    void *p = shared_only ? NULL : lv_tlsf_memalign(cpu_heap, alignment, size);
    if (!p)
        p = lv_tlsf_memalign(shared_heap, alignment, size);

    unlock_heap(interrupt_mask);

    if (!p)
        errno = ENOMEM;

    return p;
}

void *app_malloc(size_t size)
{
    return app_memalign(_Alignof(max_align_t), size, false);
}

void *app_malloc_shared(size_t size)
{
    return app_memalign(8, size, true);
}

void app_free(void *ptr)
{
    if (!ptr)
        return;

    uint32_t  interrupt_mask = lock_heap();
    lv_tlsf_t heap           = heap_for_pointer(ptr);
    if (heap)
        lv_tlsf_free(heap, ptr);
    unlock_heap(interrupt_mask);
}

void *app_calloc(size_t count, size_t size)
{
    if (size && count > SIZE_MAX / size)
    {
        errno = ENOMEM;
        return NULL;
    }
    size_t bytes = count * size;
    void  *p     = app_malloc(bytes);
    if (p)
        memset(p, 0, bytes);
    return p;
}

void *app_realloc(void *ptr, size_t size)
{
    if (!ptr)
        return app_malloc(size);
    if (!size)
    {
        app_free(ptr);
        return NULL;
    }
    uint32_t  interrupt_mask = lock_heap();
    lv_tlsf_t heap           = heap_for_pointer(ptr);
    if (!heap)
    {
        unlock_heap(interrupt_mask);
        return NULL;
    }
    size_t old_size = lv_tlsf_block_size(ptr);
    if (size <= old_size)
    {
        /* Shrink is in-place, does not copy and retains original alignment. */
        void *p = lv_tlsf_realloc(heap, ptr, size);
        unlock_heap(interrupt_mask);
        return p;
    }
    bool shared_only = heap == shared_heap;
    unlock_heap(interrupt_mask);

    /* Preserve shared capability across growth, and leave old data allocated
     * on failure. Copying with IRQs enabled avoids long display-IRQ latency. */
    void *p = app_memalign(_Alignof(max_align_t), size, shared_only);
    if (p)
    {
        memcpy(p, ptr, old_size);
        app_free(ptr);
    }
    return p;
}

static bool pause_tasks(void)
{
    configASSERT(__get_IPSR() == 0);
    bool running = xTaskGetSchedulerState() != taskSCHEDULER_NOT_STARTED;
    if (running)
        vTaskSuspendAll();
    return running;
}

static void walk_block(void *ptr, size_t size, int used, void *context)
{
    (void)ptr;
    app_memory_pool_stats_t *pool_stats = context;
    if (used)
        ++pool_stats->used_blocks;
    else
    {
        ++pool_stats->free_blocks;
        pool_stats->free += size;
        if (size > pool_stats->largest)
            pool_stats->largest = size;
    }
}

void app_memory_stats(app_memory_pool_stats_t *cpu, app_memory_pool_stats_t *shared)
{
    bool paused = pause_tasks();
    *cpu        = (app_memory_pool_stats_t){
               .total = (uintptr_t)&__app_cpu_heap_end__ - (uintptr_t)&__app_cpu_heap_start__};
    *shared = (app_memory_pool_stats_t){
        .total = (uintptr_t)&__app_shared_heap_end__ - (uintptr_t)&__app_shared_heap_start__};
    configASSERT(cpu_heap && shared_heap);
    lv_tlsf_walk_pool(lv_tlsf_get_pool(cpu_heap), walk_block, cpu);
    lv_tlsf_walk_pool(lv_tlsf_get_pool(shared_heap), walk_block, shared);
    if (paused)
        (void)xTaskResumeAll();
}

bool app_memory_check(void)
{
    bool paused = pause_tasks();
    bool valid  = cpu_heap && shared_heap && !lv_tlsf_check(cpu_heap) &&
                 !lv_tlsf_check(shared_heap) && !lv_tlsf_check_pool(lv_tlsf_get_pool(cpu_heap)) &&
                 !lv_tlsf_check_pool(lv_tlsf_get_pool(shared_heap));
    if (paused)
        (void)xTaskResumeAll();
    return valid;
}
