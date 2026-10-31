#include "./lvgl_display.h"
#include "watch_gpu_renderer.h"
#include "app_memory.h"
#include "nema_error.h"
#include "nema_graphics.h"
#include "nema_sys_defs.h"
#include "watch_gpu.h"
#include "watch_memory.h"
#include "watch_framebuffer.h"
#include "watch_gpu_copy.h"
#include "semphr.h"

static lv_disp_drv_t      disp_drv;
static SemaphoreHandle_t  panel_dma_complete;
static watch_dirty_rows_t dirty_rows;

/* With two panel frames, rendering the next refresh overlaps the preceding
 * frame's TE wait/DMA. Block only before reusing the panel transfer slot. */
static void wait_panel_idle(void)
{
    while (display_is_busy())
    {
        uint32_t   start     = watch_profile_start();
        BaseType_t completed = xSemaphoreTake(panel_dma_complete, pdMS_TO_TICKS(1000));
        WATCH_PROFILE_ADD(panel_wait_cycles, start);
        configASSERT(completed == pdTRUE || !display_is_busy());
    }
}

static volatile bool    display_sleep_requested;
static volatile bool    display_awake;
static volatile uint8_t requested_brightness = 175;
static uint8_t          applied_brightness   = 175;
static lv_indev_drv_t   indev_drv;
/* Keep two complete RGB565 panel images independent of the small GPU buffers.
 * Submit dirty rows only on LVGL's last flush: the TE-driven panel transport
 * sees one transfer per refresh, not one panel refresh per 64-line stripe. */
__attribute__((section(".display_buffer"),
    aligned(32))) static uint16_t panel_pixels[display_width * display_height];
static uint16_t                  *panel_back_pixels;
static uint16_t                  *panel_latest = panel_pixels;
static uint16_t                  *panel_drawing;
_Static_assert(sizeof(lv_color_t) == 2, "Native renderer requires RGB565");

void lvgl_display_init(void *pvParameters)
{
    while (!watch_gpu_ready)
        vTaskDelay(pdMS_TO_TICKS(1));
    panel_dma_complete = xSemaphoreCreateBinary();
    configASSERT(panel_dma_complete != NULL);
    memset(panel_pixels, 0, sizeof(panel_pixels));
    watch_gpu_cpu_write_complete();
    void *back_alloc = app_malloc_shared(sizeof(panel_pixels) + 31u);
    if (!back_alloc)
    {
        am_util_stdio_printf("Second panel frame allocation failed; stopped.\r\n");
        configASSERT(0);
    }
    panel_back_pixels = (uint16_t *)(((uintptr_t)back_alloc + 31u) & ~(uintptr_t)31u);
    configASSERT((uintptr_t)panel_back_pixels >= 0x10060000u);
    configASSERT((uintptr_t)panel_back_pixels + sizeof(panel_pixels) <= 0x10160000u);
    am_util_stdio_printf("Panel pipeline: double RGB565 frames, render/DMA overlap.\r\n");
    display_init();
    touch_init();

    static lv_disp_draw_buf_t draw_buf;
    lv_disp_draw_buf_init(&draw_buf, display_buffer1, display_buffer2, DRAW_BUF_SIZE);

    lv_disp_drv_init(&disp_drv);
    disp_drv.hor_res         = display_width;
    disp_drv.ver_res         = display_height;
    disp_drv.flush_cb        = disp_flush;
    disp_drv.draw_ctx_init   = watch_gpu_draw_ctx_init;
    disp_drv.draw_ctx_deinit = watch_gpu_draw_ctx_deinit;
    disp_drv.draw_ctx_size   = sizeof(watch_gpu_draw_ctx_t);
    disp_drv.draw_buf        = &draw_buf;
    disp_drv.full_refresh    = 0;
    disp_drv.rounder_cb      = lv_rounder_cb;
    lv_disp_drv_register(&disp_drv);

    lv_indev_drv_init(&indev_drv);
    indev_drv.type    = LV_INDEV_TYPE_POINTER;
    indev_drv.read_cb = touchpad_read;
    lv_indev_drv_register(&indev_drv);

#if LV_USE_LOG
    lv_log_register_print_cb(lvgl_log_cb);
#endif

    screen_menu_create();
    watch_memory_monitor_start();
    watch_memory_report();
    display_awake = true;
    //ui_init();

    while (1)
    {
        uint32_t delay_ms = 5;
        /* All LVGL/GPU/panel state changes stay in this task. Never suspend it
         * from a sensor task in the middle of command-list or DMA work. */
        if (display_sleep_requested)
        {
            if (display_awake)
            {
                watch_gpu_renderer_wait();
                while (display_is_busy())
                    vTaskDelay(pdMS_TO_TICKS(1));
                display_end();
                display_awake = false;
            }
        }
        else
        {
            if (!display_awake)
            {
                display_init();
                applied_brightness = 175;
                lv_obj_invalidate(lv_scr_act());
                display_awake = true;
            }
            uint8_t next_brightness = requested_brightness;
            if (next_brightness != applied_brightness && !display_is_busy())
            {
                display_set_brightness(next_brightness);
                applied_brightness = next_brightness;
            }
            uint32_t start         = watch_profile_start();
            uint32_t next_timer_ms = lv_timer_handler();
            WATCH_PROFILE_ADD(handler_cycles, start);
            if (next_timer_ms < delay_ms)
                delay_ms = next_timer_ms;
            watch_gpu_profile_poll();
        }
        TickType_t delay_ticks = pdMS_TO_TICKS(delay_ms);
        vTaskDelay(delay_ticks ? delay_ticks : 1);
    }
}

void lv_rounder_cb(lv_disp_drv_t *disp_drv, lv_area_t *area)
{
    /* Keep RGB565 row pitches 64-bit aligned, including narrow invalidations. */
    area->x1 &= ~3;
    area->x2 |= 3;
}

void lvgl_log_cb(const char *buf)
{
    am_util_stdio_printf("%s", buf);
}

static bool refresh_overwrites_full_screen(lv_disp_drv_t *driver)
{
    /* First flush can be a 64-line stripe even for a full-screen refresh.
     * Inspect the refresh owner's original invalid rectangles, not its stripe.
     * If coverage isn't provable, preserve the preceding image as before. */
    lv_disp_t *refresh = _lv_refr_get_disp_refreshing();
    if (!refresh || refresh->driver != driver)
        return false;
    for (uint16_t i = 0; i < refresh->inv_p; ++i)
    {
        const lv_area_t *a = &refresh->inv_areas[i];
        if (!refresh->inv_area_joined[i] && a->x1 <= 0 && a->y1 <= 0 &&
            a->x2 >= display_width - 1 && a->y2 >= display_height - 1)
            return true;
    }
    return false;
}

void disp_flush(lv_disp_drv_t *display, const lv_area_t *area, lv_color_t *px_map)
{
    /* LVGL waits for the draw context before flushing; repeat defensively. */
    watch_gpu_renderer_wait();
    uint32_t width  = (uint32_t)lv_area_get_width(area);
    uint32_t height = (uint32_t)lv_area_get_height(area);
    configASSERT(area->x1 >= 0 && area->y1 >= 0);
    configASSERT(area->x2 < display_width && area->y2 < display_height);
    configASSERT(width * height <= DRAW_BUF_SIZE);

    if (!dirty_rows.valid)
    {
        /* The latest submitted frame can be read concurrently by GPU and
         * MSPI. Never write that frame. Snapshot partial refreshes into the other frame
         * to retain untouched pixels. A proven full redraw needs no snapshot. */
        panel_drawing = panel_latest == panel_pixels ? panel_back_pixels : panel_pixels;
        configASSERT(panel_drawing != panel_latest);
        if (!refresh_overwrites_full_screen(display))
            watch_gpu_copy_frame(panel_drawing, panel_latest, display_width, display_height);
    }
    uint32_t stage_start = watch_profile_start();
    watch_gpu_copy_rect(&panel_drawing[(size_t)area->y1 * display_width + area->x1],
        display_width,
        px_map,
        width,
        height);
    WATCH_PROFILE_ADD(stage_cycles, stage_start);
    watch_dirty_rows_include(&dirty_rows, (unsigned)area->y1, (unsigned)area->y2);

    if (!lv_disp_flush_is_last(display))
    {
        /* Pixels are now owned by the persistent frame. No panel DMA was
         * started, so LVGL can immediately reuse this partial draw buffer. */
        lv_disp_flush_ready(display);
        return;
    }

    /* Submit exactly once for all stripes and all invalid areas in this refresh.
     * The untouched parts of a partial refresh retain their previous pixels. */
    wait_panel_idle();
    while (xSemaphoreTake(panel_dma_complete, 0) == pdTRUE)
    {
    }
    configASSERT(dirty_rows.valid);
    /* A full-width row range is contiguous in the persistent frame. Keep
     * partial updates bounded vertically instead of transmitting 490 rows
     * when only a small label changed. Gaps retain their previous pixels. */
    lv_area_t panel_area = {.x1 = 0,
        .y1                     = (lv_coord_t)dirty_rows.first,
        .x2                     = display_width - 1,
        .y2                     = (lv_coord_t)dirty_rows.last};
    __DSB();
    am_hal_daxi_control(AM_HAL_DAXI_CONTROL_FLUSH, NULL);
    display_update_area(
        &panel_area, (uint8_t *)&panel_drawing[dirty_rows.first * display_width], disp_flush_done);
    panel_latest     = panel_drawing;
    dirty_rows.valid = false;
    /* GPU staging is complete. MSPI owns panel_latest, not the small LVGL
     * draw buffer; allow the next refresh to render into the other frame. */
    lv_disp_flush_ready(display);
}

void disp_flush_done(void *context)
{
    (void)context;
    BaseType_t higher_priority_task_woken = pdFALSE;
    ++watch_gpu_stats.panel_frames;
    xSemaphoreGiveFromISR(panel_dma_complete, &higher_priority_task_woken);
    portYIELD_FROM_ISR(higher_priority_task_woken);
}

void touchpad_read(lv_indev_drv_t *drv, lv_indev_data_t *data)
{
    touch_read_data(&data->point.x, &data->point.y, &touch_available);

    data->state = touch_available ? LV_INDEV_STATE_PRESSED : LV_INDEV_STATE_RELEASED;
}

/* Task-context requests; the LVGL task acknowledges at a safe frame boundary. */
void display_turn_off(void)
{
    configASSERT(xTaskGetCurrentTaskHandle() != xLvglTask);
    display_sleep_requested = true;
    while (display_awake)
        vTaskDelay(pdMS_TO_TICKS(1));
}

void display_turn_on(void)
{
    configASSERT(xTaskGetCurrentTaskHandle() != xLvglTask);
    display_sleep_requested = false;
    while (!display_awake)
        vTaskDelay(pdMS_TO_TICKS(1));
}

void display_request_brightness(uint8_t brightness)
{
    requested_brightness = brightness;
}
