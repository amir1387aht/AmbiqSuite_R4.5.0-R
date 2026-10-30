#include "./lvgl_display.h"
#include "lv_gpu_ambiq_nema.h"
#include "nema_error.h"
#include "nema_graphics.h"
#include "nema_sys_defs.h"
#include "watch_gpu.h"
#include "watch_rgb565.h"

static lv_disp_drv_t disp_drv;
static volatile bool display_sleep_requested;
static volatile bool display_awake;
static volatile uint8_t requested_brightness = 175;
static uint8_t applied_brightness = 175;
static lv_indev_drv_t indev_drv;
static img_obj_t gpu_framebuffer = { .w = display_width, .h = DRAW_BUF_LINES,
                                    .format = NEMA_BGRA8888 };
/* LVGL serializes flushes until disp_flush_done, so one transport buffer is
 * sufficient while two independent BGRA draw buffers allow GPU/MSPI overlap. */
__attribute__((section(".display_buffer"), aligned(32)))
static uint16_t panel_pixels[DRAW_BUF_SIZE];
_Static_assert(sizeof(lv_color_t) == 4, "Ambiq GPU archive requires 32-bit color");

void lvgl_display_init(void* pvParameters) {
    while (!watch_gpu_ready) vTaskDelay(pdMS_TO_TICKS(1));
    display_init();
    touch_init();

    static lv_disp_draw_buf_t draw_buf;
    lv_disp_draw_buf_init(&draw_buf, display_buffer1, display_buffer2, DRAW_BUF_SIZE);

    lv_disp_drv_init(&disp_drv);
    disp_drv.hor_res = display_width;
    disp_drv.ver_res = display_height;
    disp_drv.flush_cb = disp_flush;
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
    uint32_t pixel_count = (uint32_t)lv_area_get_width(area) * lv_area_get_height(area);
    configASSERT(pixel_count <= DRAW_BUF_SIZE);
    for (uint32_t i = 0; i < pixel_count; ++i) {
        panel_pixels[i] = watch_bgra8888_to_rgb565(px_map[i].full);
    }
    __DSB();
    am_hal_daxi_control(AM_HAL_DAXI_CONTROL_FLUSH, NULL);
    display_update_area(area, (uint8_t *)panel_pixels, disp_flush_done);
}

void disp_flush_done(void *context) {
    (void)context;
    lv_disp_flush_ready(&disp_drv);
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
