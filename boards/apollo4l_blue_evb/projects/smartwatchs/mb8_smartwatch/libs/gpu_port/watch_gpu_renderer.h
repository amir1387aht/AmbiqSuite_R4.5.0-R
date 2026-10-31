#ifndef WATCH_GPU_RENDERER_H
#define WATCH_GPU_RENDERER_H

#include "lvgl.h"
#include "src/draw/sw/lv_draw_sw.h"

#if LVGL_VERSION_MAJOR != 8 || LVGL_VERSION_MINOR != 4
#error "This draw-context backend requires LVGL 8.4"
#endif

typedef struct
{
    lv_draw_sw_ctx_t software; /* Must be first: LVGL's software context ABI. */
    lv_disp_drv_t   *display_driver;
    unsigned         software_depth;
} watch_gpu_draw_ctx_t;

void watch_gpu_renderer_init(void);
void watch_gpu_renderer_wait(void);
void watch_gpu_draw_ctx_init(lv_disp_drv_t *driver, lv_draw_ctx_t *ctx);
void watch_gpu_draw_ctx_deinit(lv_disp_drv_t *driver, lv_draw_ctx_t *ctx);

#endif
