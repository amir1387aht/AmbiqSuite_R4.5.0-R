/* Linker reservations and allocator snapshots are different views of the same
 * RAM. Never add RTOS/LVGL heap reservations to their live allocations twice.
 * This is a startup snapshot, not a stack high-water or physical RAM probe. */
#include "watch_memory.h"
#include "lvgl.h"
#include "FreeRTOS.h"
#include "task.h"
#include "am_util.h"
#include "watch_glyph_cache.h"
#include "drivers/display/display.h"
#include <stdint.h>

extern unsigned char __cpu_ram_start__, __cpu_ram_end__, _edata;
extern unsigned char __shared_ram_start__, __shared_ram_end__, __shared_static_end__;
extern unsigned char __lvgl_tcm_start__, __lvgl_tcm_end__;
extern unsigned char __lvgl_ssram_start__, __lvgl_ssram_end__;
extern unsigned char __stack_start__, __stack_end__, __heap_start__, __heap_end__;
extern unsigned char __display_buffers_start__, __display_buffers_end__;

#define ADDRESS(s) ((uintptr_t)&(s))
#define SPAN(a,b) (ADDRESS(b) - ADDRESS(a))
#define UL(v) ((unsigned long)(v))

static void print_pool(const char *name, const lv_mem_monitor_t *pool)
{
    configASSERT(pool->free_size <= pool->total_size);
    am_util_stdio_printf("  %s: used=%lu / total=%lu - free=%lu; largest=%lu; free_blocks=%lu; frag=%u%%\r\n",
        name, UL(pool->total_size - pool->free_size), UL(pool->total_size),
        UL(pool->free_size), UL(pool->free_biggest_size), UL(pool->free_cnt),
        (unsigned)pool->frag_pct);
}

void watch_memory_report(void)
{
    lv_mem_monitor_t ui, gpu;
    HeapStats_t rtos;
    /* Single LVGL owner; freeze task-side RTOS heap changes only while taking
     * the snapshot. Never print blocking UART text with the scheduler stopped. */
    vTaskSuspendAll();
    lv_mem_monitor(&ui);
    lv_mem_ssram_monitor(&gpu);
    vPortGetHeapStats(&rtos);
    (void)xTaskResumeAll();

    size_t cpu_total = SPAN(__cpu_ram_start__, __cpu_ram_end__);
    size_t cpu_reserved = SPAN(__cpu_ram_start__, _edata);
    size_t shared_total = SPAN(__shared_ram_start__, __shared_ram_end__);
    size_t shared_reserved = SPAN(__shared_ram_start__, __shared_static_end__);
    size_t ui_total = SPAN(__lvgl_tcm_start__, __lvgl_tcm_end__);
    size_t gpu_total = SPAN(__lvgl_ssram_start__, __lvgl_ssram_end__);
    configASSERT(cpu_reserved <= cpu_total && shared_reserved <= shared_total);
    configASSERT(ui.total_size == ui_total && gpu.total_size == gpu_total);
    configASSERT(rtos.xAvailableHeapSpaceInBytes <= configTOTAL_HEAP_SIZE);
    size_t total = cpu_total + shared_total + ui_total + gpu_total;
    size_t unassigned = cpu_total - cpu_reserved + shared_total - shared_reserved;
    size_t reserved = total - unassigned;
    size_t heap_free = ui.free_size + gpu.free_size + rtos.xAvailableHeapSpaceInBytes;
    configASSERT(heap_free <= reserved);
    am_util_stdio_printf("\r\n=== RAM startup snapshot (bytes; configured banks, not a hardware probe) ===\r\n");
    am_util_stdio_printf("RAM total=%lu bytes (%lu KiB); CPU TCM=%lu; GPU-visible shared SRAM=%lu\r\n",
        UL(total), UL(total / 1024u), UL(cpu_total + ui_total), UL(shared_total + gpu_total));
    am_util_stdio_printf("RAM accounted: used_or_reserved=%lu / total=%lu - free=%lu\r\n",
        UL(reserved - heap_free), UL(total), UL(unassigned + heap_free));
    am_util_stdio_printf("  free = reusable_heap=%lu + linker_unassigned=%lu (NOT one allocatable block)\r\n",
        UL(heap_free), UL(unassigned));
    am_util_stdio_printf("Linker: reserved=%lu / total=%lu - unassigned=%lu\r\n",
        UL(reserved), UL(total), UL(unassigned));
    am_util_stdio_printf("  CPU TCM: reserved=%lu / total=%lu - unassigned=%lu\r\n",
        UL(cpu_reserved), UL(cpu_total), UL(cpu_total - cpu_reserved));
    am_util_stdio_printf("  General shared: reserved=%lu / total=%lu - unassigned=%lu\r\n",
        UL(shared_reserved), UL(shared_total), UL(shared_total - shared_reserved));
    print_pool("LVGL UI TCM heap", &ui);
    print_pool("LVGL/Nema shared heap", &gpu);
    am_util_stdio_printf("  RTOS heap: used=%lu / total=%lu - free=%lu; min_ever_free=%lu; largest=%lu; free_blocks=%lu\r\n",
        UL(configTOTAL_HEAP_SIZE - rtos.xAvailableHeapSpaceInBytes), UL(configTOTAL_HEAP_SIZE),
        UL(rtos.xAvailableHeapSpaceInBytes), UL(rtos.xMinimumEverFreeBytesRemaining),
        UL(rtos.xSizeOfLargestFreeBlockInBytes), UL(rtos.xNumberOfFreeBlocks));
    am_util_stdio_printf("Reserved: C heap=%lu (live use unmeasured); main/IRQ stack=%lu; task stacks included in RTOS use\r\n",
        UL(SPAN(__heap_start__, __heap_end__)), UL(SPAN(__stack_start__, __stack_end__)));
    am_util_stdio_printf("Display: LVGL 2 x %lu-byte BGRA8888 draw buffers (%u x %u each); RGB565 panel 2 x %lu bytes\r\n",
        UL(sizeof(display_buffer1)), (unsigned)display_width, (unsigned)DRAW_BUF_LINES,
        UL((size_t)display_width * display_height * sizeof(uint16_t)));
    am_util_stdio_printf("  Static display section=%lu; second panel frame is inside Nema heap, not additional RAM\r\n",
        UL(SPAN(__display_buffers_start__, __display_buffers_end__)));
    am_util_stdio_printf("  Glyph arena: filled=%lu / reserved=%lu (inside Nema heap; empty cache space is NOT heap-free)\r\n",
        UL(watch_gpu_glyph_cache_used()), UL(watch_gpu_glyph_cache_capacity()));
    am_util_stdio_printf("Accounting includes allocator metadata and reserved stacks/C heap as used; no live stack scan.\r\n=== End RAM startup snapshot ===\r\n\r\n");
}
