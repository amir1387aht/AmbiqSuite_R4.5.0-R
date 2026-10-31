/* GPU initialization for the original watch application; upstream LVGL v8.4 draw context. */
#include "watch_gpu.h"
#include "lvgl.h"
#include "watch_gpu_renderer.h"
#include "nema_error.h"
#include "nema_sys_defs.h"
#include "am_mcu_apollo.h"
#include "am_util.h"
#include "FreeRTOS.h"
#include "watch_glyph_cache.h"
#include "watch_gpu_copy.h"
#include "watch_tick.h"
#if !LV_USE_PERF_MONITOR || !LV_USE_LABEL
#error "This watch build requires the FPS monitor, labels and Nema GPU enabled"
#endif

volatile bool              watch_gpu_ready;
volatile watch_gpu_stats_t watch_gpu_stats;
static int32_t             first_text_submission = -1;
static bool                text_completion_reported;

/* Commit CPU-produced shared pixels before GPU reads or an IRQ invalidates DAXI. */
void watch_gpu_cpu_write_complete(void)
{
    __DSB();
    am_hal_daxi_control(AM_HAL_DAXI_CONTROL_FLUSH, NULL);
}

void watch_gpu_note_submit(int32_t submission_id, uint32_t glyphs)
{
    ++watch_gpu_stats.submitted_cl;
    if (glyphs && first_text_submission < 0)
        first_text_submission = submission_id;
}

void watch_gpu_note_completed(int32_t submission_id, uint32_t hardware_id)
{
    watch_gpu_stats.completed_hw_id = hardware_id;
    /* Initial IDs only: do not infer long-running ordering across 24-bit wrap. */
    if (!text_completion_reported && first_text_submission >= 0 &&
        submission_id >= first_text_submission && hardware_id >= (uint32_t)submission_id)
    {
        text_completion_reported = true;
        am_util_stdio_printf("GPU TEXT completed: submitted=%ld HW_CLID=%lu glyphs=%lu\r\n",
            (long)submission_id,
            (unsigned long)hardware_id,
            (unsigned long)watch_gpu_stats.gpu_glyphs);
    }
}

#if LV_USE_LOG
static void log_line(const char *s)
{
    am_util_stdio_printf("%s", s);
}
#endif

void watch_gpu_init(void)
{
    /* Must run AFTER SRAM power config, BEFORE any MSPI/IOM DMA user.
     * Apollo4l HAL DMA entry points can indirectly call nema_init(). */
#if LV_USE_LOG
    lv_log_register_print_cb(log_line);
#endif
    lv_init();
    watch_gpu_renderer_init();
    am_gpu_power_enable(AM_GPU_PWRCTRL_USER);
    if (nema_get_error() != NEMA_ERR_NO_ERROR || lv_mem_test() != LV_RES_OK)
    {
        am_util_stdio_printf("GPU/heap initialization failed.\r\n");
        configASSERT(0);
    }
    watch_gpu_glyph_cache_init();
    watch_gpu_copy_init();
#if WATCH_GPU_PROFILE
    CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;
    DWT->CYCCNT = 0;
    DWT->CTRL |= DWT_CTRL_CYCCNTENA_Msk;
#endif
    am_util_stdio_printf("LVGL 8.4.0 GPU: FPS monitor=%u refresh=%u ms profile=%u\r\n",
        (unsigned)LV_USE_PERF_MONITOR,
        (unsigned)LV_DISP_DEF_REFR_PERIOD,
        (unsigned)WATCH_GPU_PROFILE);
    watch_gpu_profile_init();
    am_util_stdio_printf("GPU IRQ: immediate RTOS wake; profile UART below LVGL.\r\n");
    watch_gpu_ready = true;
    am_util_stdio_printf(
        "Native RGB565: LVGL 8.4.0 / LVGL16 -> Nema RGB565 -> RGB565 panel; no BGRA conversion.\r\n");
}
