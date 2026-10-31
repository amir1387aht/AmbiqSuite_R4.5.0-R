/* Capture on the LVGL task; format/transmit only on a lower-priority task. */
#include "watch_gpu.h"
#if WATCH_GPU_PROFILE
#include <stdio.h>
#include "FreeRTOS.h"
#include "queue.h"
#include "task.h"
#include "watch_tick.h"
#include "drivers/uart/uart_output.h"
#define TRANSFER_TIME "stage_ms"
#define TRANSFER_GPU "stage_gpu"
#define TRANSFER_CPU "stage_cpu"

typedef struct {
    watch_gpu_stats_t current;
    watch_gpu_stats_t previous;
    uint32_t elapsed_ms;
} watch_profile_sample_t;
static QueueHandle_t profile_queue;

static void profile_output_task(void *argument)
{
    (void)argument;
    watch_profile_sample_t sample;
    char line[768];
    for (;;) {
        if (xQueueReceive(profile_queue, &sample, portMAX_DELAY) != pdTRUE) continue;
        const watch_gpu_stats_t *c = &sample.current, *p = &sample.previous;
        const uint32_t cycles_per_ms = configCPU_CLOCK_HZ / 1000u;
#define DELTA(field) ((uint32_t)(c->field - p->field))
#define MS(field) ((unsigned long)(DELTA(field) / cycles_per_ms))
        int length = snprintf(line, sizeof(line),
            "GPUprof dma_fps=%lu te_hz=%lu cl=%lu glyph_gpu=%lu glyph_cpu=%lu cache_new=%lu cache_hit=%lu handler_ms=%lu " TRANSFER_TIME "=%lu gpuwait_ms=%lu panelwait_ms=%lu " TRANSFER_GPU "=%lu " TRANSFER_CPU "=%lu tewait_ms=%lu dma_ms=%lu framecopy_ms=%lu gap1=%lu gap2=%lu gap3p=%lu hw=%lu native565=%u fill_gpu=%lu bg_gpu=%lu cpu_fallback=%lu\r\n",
            (unsigned long)(DELTA(panel_frames) * 1000u / sample.elapsed_ms),
            (unsigned long)(DELTA(te_edges) * 1000u / sample.elapsed_ms),
            (unsigned long)DELTA(submitted_cl),
            (unsigned long)DELTA(gpu_glyphs), (unsigned long)DELTA(cpu_glyphs),
            (unsigned long)DELTA(glyph_cache_new), (unsigned long)DELTA(glyph_cache_hits),
            MS(handler_cycles), MS(stage_cycles), MS(gpu_wait_cycles), MS(panel_wait_cycles),
            (unsigned long)DELTA(stage_gpu), (unsigned long)DELTA(stage_cpu),
            MS(te_wait_cycles), MS(panel_dma_cycles), MS(frame_copy_cycles),
            (unsigned long)DELTA(panel_gap1), (unsigned long)DELTA(panel_gap2),
            (unsigned long)DELTA(panel_gap3p), (unsigned long)c->completed_hw_id, (unsigned)WATCH_NATIVE_RGB565,
            (unsigned long)DELTA(native_fill_gpu), (unsigned long)DELTA(native_bg_gpu),
            (unsigned long)DELTA(native_cpu_fallbacks));
#undef MS
#undef DELTA
        if (length > 0 && (size_t)length < sizeof(line))
            (void)uart_profile_write(line, (size_t)length);
    }
}

void watch_gpu_profile_init(void)
{
    profile_queue = xQueueCreate(1, sizeof(watch_profile_sample_t));
    configASSERT(profile_queue != NULL);
    /* LVGL is priority 2. UART polling must always be preemptible by it. */
    configASSERT(tskIDLE_PRIORITY + 1 < TASK_PRIORITY_MEDIUM);
    configASSERT(xTaskCreate(profile_output_task, "GPU UART", 1024, NULL,
                             tskIDLE_PRIORITY + 1, NULL) == pdPASS);
}

void watch_gpu_profile_poll(void)
{
    static uint32_t last_ms;
    static watch_gpu_stats_t previous;
    uint32_t now = watch_tick_ms();
    uint32_t elapsed = now - last_ms;
    if (elapsed < 1000u || !profile_queue) return;
    watch_profile_sample_t sample = { .current = watch_gpu_stats,
        .previous = previous, .elapsed_ms = elapsed };
    /* Replace a stale unsent sample, never block graphics waiting for UART. */
    xQueueOverwrite(profile_queue, &sample);
    previous = sample.current;
    last_ms = now;
}
#else
void watch_gpu_profile_init(void) {}
void watch_gpu_profile_poll(void) {}
#endif
