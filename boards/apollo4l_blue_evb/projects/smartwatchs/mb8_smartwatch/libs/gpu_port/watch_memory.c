/* A startup snapshot: configured/powered banks, not a hardware capacity probe.
 * Each byte is accounted once; shared allocations are included in app heaps. */
#include "watch_memory.h"
#include "app_memory.h"
#include "lvgl.h"
#include "FreeRTOS.h"
#include "task.h"
#include "am_util.h"
#include "watch_glyph_cache.h"
#include "drivers/display/display.h"
#include <stdint.h>
#include <stdio.h>
#include "drivers/uart/uart_output.h"

extern unsigned char __cpu_ram_start__, __cpu_ram_end__, __shared_ram_start__, __shared_ram_end__;
extern unsigned char __stack_start__, __stack_end__;
extern unsigned char __display_buffers_start__, __display_buffers_end__;

#define SPAN(a, b) ((uintptr_t) & (b) - (uintptr_t) & (a))
#define UL(v)      ((unsigned long)(v))

static void print_pool(const char *name, const app_memory_pool_stats_t *s)
{
    configASSERT(s->free <= s->total);
    am_util_stdio_printf(
        "  %s: used+metadata=%lu / total=%lu - free=%lu; largest=%lu; free_blocks=%lu\r\n",
        name,
        UL(s->total - s->free),
        UL(s->total),
        UL(s->free),
        UL(s->largest),
        UL(s->free_blocks));
}

void watch_memory_report(void)
{
    app_memory_pool_stats_t cpu_stats, shared_stats;
    HeapStats_t             rtos_stats;

    vTaskSuspendAll();
    app_memory_stats(&cpu_stats, &shared_stats);
    vPortGetHeapStats(&rtos_stats);
    (void)xTaskResumeAll();

    size_t cpu_total    = SPAN(__cpu_ram_start__, __cpu_ram_end__);
    size_t shared_total = SPAN(__shared_ram_start__, __shared_ram_end__);
    size_t total        = cpu_total + shared_total;
    size_t app_free     = cpu_stats.free + shared_stats.free;
    size_t kernel_free  = rtos_stats.xAvailableHeapSpaceInBytes;
    configASSERT(cpu_stats.total <= cpu_total && shared_stats.total <= shared_total);
    configASSERT(kernel_free <= configTOTAL_HEAP_SIZE);
    configASSERT(app_free + kernel_free <= total);

    am_util_stdio_printf(
        "\r\n=== RAM startup snapshot (bytes; configured banks, not a hardware probe) ===\r\n");
    am_util_stdio_printf(
        "RAM total=%lu bytes (%lu KiB); CPU TCM=%lu; GPU-visible shared SRAM=%lu\r\n",
        UL(total),
        UL(total / 1024u),
        UL(cpu_total),
        UL(shared_total));
    am_util_stdio_printf("RAM accounted: used_or_reserved=%lu / total=%lu - free=%lu\r\n",
        UL(total - app_free - kernel_free),
        UL(total),
        UL(app_free + kernel_free));
    am_util_stdio_printf(
        "  Application malloc free=%lu; dedicated RTOS free=%lu (NOT one contiguous block)\r\n",
        UL(app_free),
        UL(kernel_free));
    print_pool("Application CPU-only TCM", &cpu_stats);
    print_pool("Application GPU/DMA-visible shared SRAM", &shared_stats);
    am_util_stdio_printf(
        "  malloc: TCM first, shared fallback; app_malloc_shared: shared only. LVGL/Nema use these same pools.\r\n");
    am_util_stdio_printf(
        "  RTOS: used=%lu / total=%lu - free=%lu; min_ever_free=%lu; largest=%lu; free_blocks=%lu\r\n",
        UL(configTOTAL_HEAP_SIZE - kernel_free),
        UL(configTOTAL_HEAP_SIZE),
        UL(kernel_free),
        UL(rtos_stats.xMinimumEverFreeBytesRemaining),
        UL(rtos_stats.xSizeOfLargestFreeBlockInBytes),
        UL(rtos_stats.xNumberOfFreeBlocks));
    am_util_stdio_printf(
        "Static/non-app reservation=%lu (includes entire RTOS heap); linker tails now application heaps.\r\n",
        UL(total - cpu_stats.total - shared_stats.total));
    am_util_stdio_printf(
        "Main/IRQ stack reserved=%lu; task stacks included in RTOS use; no separate libc heap.\r\n",
        UL(SPAN(__stack_start__, __stack_end__)));
    am_util_stdio_printf(
        "Display: LVGL 2 x %lu-byte %s draw buffers (%u x %u); RGB565 panel 2 x %lu bytes\r\n",
        UL(sizeof(display_buffer1)),
        "RGB565",
        (unsigned)display_width,
        (unsigned)DRAW_BUF_LINES,
        UL((size_t)display_width * display_height * sizeof(uint16_t)));
    am_util_stdio_printf(
        "  Static display section=%lu; second panel frame is already included in shared heap use.\r\n",
        UL(SPAN(__display_buffers_start__, __display_buffers_end__)));
    am_util_stdio_printf(
        "  Glyph arena filled=%lu / reserved=%lu (already included in shared use, NOT heap-free).\r\n",
        UL(watch_gpu_glyph_cache_used()),
        UL(watch_gpu_glyph_cache_capacity()));
    am_util_stdio_printf(
        "Free excludes allocator metadata, reserved stacks and cached allocations; no live stack scan.\r\n=== End RAM startup snapshot ===\r\n\r\n");
}

/* Periodic diagnostics must never drain UART from the rendering task. Take a
 * consistent heap snapshot briefly, then format/write with scheduling enabled.
 * A failed diagnostic write is dropped rather than stopping the UI. */
static TaskHandle_t memory_monitor_task;

static void memory_monitor(void *argument)
{
    (void)argument;
    TickType_t       next     = xTaskGetTickCount();
    const TickType_t interval = pdMS_TO_TICKS(2000);
    configASSERT(interval > 0);

    for (;;)
    {
        vTaskDelayUntil(&next, interval);

        app_memory_pool_stats_t cpu_stats, shared_stats;
        HeapStats_t             rtos_stats;
        vTaskSuspendAll();
        app_memory_stats(&cpu_stats, &shared_stats);
        vPortGetHeapStats(&rtos_stats);
        (void)xTaskResumeAll();

        size_t total = SPAN(__cpu_ram_start__, __cpu_ram_end__) +
                       SPAN(__shared_ram_start__, __shared_ram_end__);
        size_t app_free   = cpu_stats.free + shared_stats.free;
        size_t free_total = app_free + rtos_stats.xAvailableHeapSpaceInBytes;
        configASSERT(free_total <= total);

        char line[512];
        int  length = snprintf(line,
            sizeof(line),
            "RAM used_or_reserved=%lu / total=%lu - free=%lu; app_free=%lu; "
             "TCM_free=%lu largest=%lu; shared_free=%lu largest=%lu; "
             "RTOS_free=%lu min=%lu largest=%lu\r\n",
            UL(total - free_total),
            UL(total),
            UL(free_total),
            UL(app_free),
            UL(cpu_stats.free),
            UL(cpu_stats.largest),
            UL(shared_stats.free),
            UL(shared_stats.largest),
            UL(rtos_stats.xAvailableHeapSpaceInBytes),
            UL(rtos_stats.xMinimumEverFreeBytesRemaining),
            UL(rtos_stats.xSizeOfLargestFreeBlockInBytes));

        if (length > 0 && (size_t)length < sizeof(line))
            (void)uart_log_write(line, (size_t)length);
    }
}

void watch_memory_monitor_start(void)
{
    /* Called once by the LVGL owner after UI creation; repeat calls are safe. */
    if (memory_monitor_task)
        return;
    configASSERT(tskIDLE_PRIORITY + 1 < TASK_PRIORITY_MEDIUM);
    configASSERT(
        xTaskCreate(
            memory_monitor, "RAM UART", 1024, NULL, tskIDLE_PRIORITY + 1, &memory_monitor_task) ==
        pdPASS);
}
