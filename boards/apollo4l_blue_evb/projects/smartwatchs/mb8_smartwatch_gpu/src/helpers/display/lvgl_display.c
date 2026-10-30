#include "./lvgl_display.h"
#include "lv_gpu_ambiq_nema.h"
#include "nema_error.h"
#include "nema_graphics.h"
#include "nema_sys_defs.h"
#include "watch_gpu.h"
#include "watch_rgb565.h"
#include "watch_framebuffer.h"
#include "semphr.h"

static lv_disp_drv_t disp_drv;
static SemaphoreHandle_t panel_dma_complete;
static watch_dirty_rows_t dirty_rows;

/* LVGL calls this while draw_buf->flushing is set. Do not burn CPU spinning
 * for TE or DMA completion; the MSPI ISR releases the dedicated semaphore. */
static void disp_wait(lv_disp_drv_t *driver)
{
    if (driver->draw_buf->flushing) {
        BaseType_t completed = xSemaphoreTake(panel_dma_complete, pdMS_TO_TICKS(1000));
        configASSERT(completed == pdTRUE || !driver->draw_buf->flushing);
    }
}
static volatile bool display_sleep_requested;
static volatile bool display_awake;
static volatile uint8_t requested_brightness = 175;
static uint8_t applied_brightness = 175;
static lv_indev_drv_t indev_drv;
static img_obj_t gpu_framebuffer = { .w = display_width, .h = DRAW_BUF_LINES,
                                    .format = NEMA_BGRA8888 };
/* Keep a complete RGB565 panel image, independent of the small GPU draw buffers.
 * Submit dirty rows only on LVGL's last flush: the TE-driven panel transport
 * sees one transfer per refresh, not one panel refresh per 64-line stripe. */
__attribute__((section(".display_buffer"), aligned(32)))
static uint16_t panel_pixels[display_width * display_height];
_Static_assert(sizeof(lv_color_t) == 4, "Ambiq GPU archive requires 32-bit color");

void lvgl_display_init(void* pvParameters) {
    while (!watch_gpu_ready) vTaskDelay(pdMS_TO_TICKS(1));
    panel_dma_complete = xSemaphoreCreateBinary();
    configASSERT(panel_dma_complete != NULL);
    memset(panel_pixels, 0, sizeof(panel_pixels));
    display_init();
    touch_init();

    static lv_disp_draw_buf_t draw_buf;
    lv_disp_draw_buf_init(&draw_buf, display_buffer1, display_buffer2, DRAW_BUF_SIZE);

    lv_disp_drv_init(&disp_drv);
    disp_drv.hor_res = display_width;
    disp_drv.ver_res = display_height;
    disp_drv.flush_cb = disp_flush;
    disp_drv.wait_cb = disp_wait;
    disp_drv.gpu_wait_cb = lv_gpu_ambiq_nema_wait;
    disp_drv.draw_buf = &draw_buf;
    disp_drv.user_data = &gpu_framebuffer;
    disp_drv.full_refresh = 0;
    disp_drv.rounder_cb = lv_rounder_cb;
    lv_disp_drv_register(&disp_drv);

    lv_indev_drv_init(&indev_drv);
    indev_drv.type = LV_INDEV_TYPE_POINTER;
    indev_drv.read_cb = touchpad_read;
    lv_indev_drv_register(&indev_drv);

#if LV_USE_LOG
    lv_log_register_print_cb(lvgl_log_cb);
#endif

    screen_menu_create();
    display_awake = true;
    //ui_init();

    while (1) {
        /* All LVGL/GPU/panel state changes stay in this task. Never suspend it
         * from a sensor task in the middle of command-list or DMA work. */
        if (display_sleep_requested) {
            if (display_awake) {
                lv_gpu_ambiq_nema_wait(&disp_drv);
                while (display_is_busy()) vTaskDelay(pdMS_TO_TICKS(1));
                display_end();
                display_awake = false;
            }
        } else {
            if (!display_awake) {
                display_init();
                applied_brightness = 175;
                lv_obj_invalidate(lv_scr_act());
                display_awake = true;
            }
            uint8_t next_brightness = requested_brightness;
            if (next_brightness != applied_brightness && !display_is_busy()) {
                display_set_brightness(next_brightness);
                applied_brightness = next_brightness;
            }
            lv_timer_handler();
        }
        vTaskDelay(pdMS_TO_TICKS(5));
    }
}

void lv_rounder_cb(lv_disp_drv_t *disp_drv, lv_area_t *area)
{
    /* 1) Align the start to an even column */
    if (area->x1 & 1) area->x1--;
    /* 2) Ensure width is even (x2 becomes odd if needed) */
    if (((area->x2 - area->x1 + 1) & 1) != 0) area->x2++;
}

void lvgl_log_cb(const char * buf)
{
    am_util_stdio_printf("%s", buf);
}

void disp_flush(lv_disp_drv_t* display, const lv_area_t* area, lv_color_t * px_map) {
    /* LVGL calls gpu_wait_cb first; repeat defensively for direct flush calls. */
    lv_gpu_ambiq_nema_wait(display);
    uint32_t width = (uint32_t)lv_area_get_width(area);
    uint32_t height = (uint32_t)lv_area_get_height(area);
    configASSERT(area->x1 >= 0 && area->y1 >= 0);
    configASSERT(area->x2 < display_width && area->y2 < display_height);
    configASSERT(width * height <= DRAW_BUF_SIZE);

    /* LVGL waits for any outstanding DMA before entering this callback. The
     * persistent panel frame must never be modified while DMA reads from it. */
    watch_bgra8888_rect_to_rgb565(
        &panel_pixels[(size_t)area->y1 * display_width + area->x1],
        display_width, px_map, width, height);
    watch_dirty_rows_include(&dirty_rows, (unsigned)area->y1, (unsigned)area->y2);

    if (!lv_disp_flush_is_last(display)) {
        /* Pixels are now owned by the persistent frame. No panel DMA was
         * started, so LVGL can immediately reuse this partial draw buffer. */
        lv_disp_flush_ready(display);
        return;
    }

    /* Submit exactly once for all stripes and all invalid areas in this refresh.
     * The untouched parts of a partial refresh retain their previous pixels. */
    while (xSemaphoreTake(panel_dma_complete, 0) == pdTRUE) {}
    configASSERT(dirty_rows.valid);
    /* A full-width row range is contiguous in the persistent frame. Keep
     * partial updates bounded vertically instead of transmitting 490 rows
     * when only a small label changed. Gaps retain their previous pixels. */
    lv_area_t panel_area = {
        .x1 = 0, .y1 = (lv_coord_t)dirty_rows.first,
        .x2 = display_width - 1, .y2 = (lv_coord_t)dirty_rows.last
    };
    __DSB();
    am_hal_daxi_control(AM_HAL_DAXI_CONTROL_FLUSH, NULL);
    display_update_area(&panel_area,
        (uint8_t *)&panel_pixels[dirty_rows.first * display_width], disp_flush_done);
    dirty_rows.valid = false;
}

void disp_flush_done(void *context) {
    (void)context;
    BaseType_t higher_priority_task_woken = pdFALSE;
    lv_disp_flush_ready(&disp_drv);
    xSemaphoreGiveFromISR(panel_dma_complete, &higher_priority_task_woken);
    portYIELD_FROM_ISR(higher_priority_task_woken);
}

void touchpad_read(lv_indev_drv_t* drv, lv_indev_data_t* data) {
    touch_read_data(&data->point.x, &data->point.y, &touch_available);

    data->state = touch_available ? LV_INDEV_STATE_PRESSED : LV_INDEV_STATE_RELEASED;
}

/* Task-context requests; the LVGL task acknowledges at a safe frame boundary. */
void display_turn_off(void) {
    configASSERT(xTaskGetCurrentTaskHandle() != xLvglTask);
    display_sleep_requested = true;
    while (display_awake) vTaskDelay(pdMS_TO_TICKS(1));
}

void display_turn_on(void) {
    configASSERT(xTaskGetCurrentTaskHandle() != xLvglTask);
    display_sleep_requested = false;
    while (!display_awake) vTaskDelay(pdMS_TO_TICKS(1));
}

void display_request_brightness(uint8_t brightness) {
    requested_brightness = brightness;
}
