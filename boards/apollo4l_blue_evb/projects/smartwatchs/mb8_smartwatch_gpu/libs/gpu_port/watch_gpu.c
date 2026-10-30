/* GPU bring-up stays on the SDK v8.1 ABI; no EVB display/PSRAM driver. */
#include "watch_gpu.h"
#include "lvgl.h"
#include "lv_gpu_ambiq_nema.h"
#include "nema_error.h"
#include "nema_sys_defs.h"
#include "am_mcu_apollo.h"
#include "am_util.h"
#include "FreeRTOS.h"
#include "task.h"

volatile bool watch_gpu_ready;
volatile uint32_t watch_gpu_submissions;
volatile uint32_t watch_gpu_completed_waits;
volatile uint32_t watch_panel_flushes;

static void log_line(const char *s) { am_util_stdio_printf("%s", s); }

void watch_gpu_init(void)
{
    /* Must run AFTER SRAM power config, BEFORE any MSPI/IOM DMA user.
     * Apollo4l HAL DMA entry points can indirectly call nema_init(). */
    lv_log_register_print_cb(log_line);
    lv_init();
    am_gpu_power_enable(AM_GPU_PWRCTRL_USER);
    if (nema_get_error() != NEMA_ERR_NO_ERROR || lv_mem_test() != LV_RES_OK) {
        am_util_stdio_printf("GPU/heap initialization failed.\r\n");
        configASSERT(0);
    }
    watch_gpu_ready = true;
    am_util_stdio_printf("Ambiq LVGL 8.1 GPU initialized; BGRA8888 -> RGB565 panel.\r\n");
}

static void animate_x(void *obj, int32_t x) { lv_obj_set_x(obj, x); }

static void report(lv_timer_t *timer)
{
    (void)timer;
    am_util_stdio_printf("GPU submits=%lu completed-waits=%lu panel-flushes=%lu\r\n",
        (unsigned long)watch_gpu_submissions, (unsigned long)watch_gpu_completed_waits,
        (unsigned long)watch_panel_flushes);
}

void watch_gpu_smoke_screen(void)
{
    static const uint32_t colors[] = {0xff0000, 0x00ff00, 0x0000ff, 0xffffff};
    lv_obj_t *screen = lv_scr_act();
    lv_obj_set_style_bg_color(screen, lv_color_black(), 0);
    for (unsigned i = 0; i < 4; ++i) {
        lv_obj_t *bar = lv_obj_create(screen);
        lv_obj_remove_style_all(bar);
        lv_obj_set_size(bar, 176, 48);
        lv_obj_set_pos(bar, 8, 8 + (int)i * 56);
        lv_obj_set_style_bg_color(bar, lv_color_hex(colors[i]), 0);
        lv_obj_set_style_bg_opa(bar, LV_OPA_COVER, 0);
    }
    lv_obj_t *moving = lv_obj_create(screen);
    lv_obj_remove_style_all(moving);
    lv_obj_set_size(moving, 64, 64);
    lv_obj_set_pos(moving, 8, 260);
    lv_obj_set_style_bg_color(moving, lv_color_hex(0xff8000), 0);
    lv_obj_set_style_bg_opa(moving, LV_OPA_70, 0);
    lv_obj_set_style_radius(moving, 16, 0);
    lv_anim_t animation;
    lv_anim_init(&animation);
    lv_anim_set_var(&animation, moving);
    lv_anim_set_exec_cb(&animation, animate_x);
    lv_anim_set_values(&animation, 8, 120);
    lv_anim_set_time(&animation, 1000);
    lv_anim_set_playback_time(&animation, 1000);
    lv_anim_set_repeat_count(&animation, LV_ANIM_REPEAT_INFINITE);
    lv_anim_start(&animation);
    lv_obj_t *label = lv_label_create(screen);
    lv_label_set_text(label, "Nema GPU\nRGB565 panel");
    lv_obj_set_pos(label, 8, 360);
    lv_obj_set_style_text_color(label, lv_color_white(), 0);
    lv_timer_create(report, 1000, NULL);
}
