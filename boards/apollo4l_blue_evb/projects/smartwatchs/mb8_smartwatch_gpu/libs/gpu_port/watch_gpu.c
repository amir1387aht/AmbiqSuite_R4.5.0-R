/* GPU initialization for the original watch application; SDK LVGL v8.1 ABI. */
#include "watch_gpu.h"
#include "lvgl.h"
#include "lv_gpu_ambiq_nema.h"
#include "nema_error.h"
#include "nema_sys_defs.h"
#include "am_mcu_apollo.h"
#include "am_util.h"
#include "FreeRTOS.h"

volatile bool watch_gpu_ready;

#if LV_USE_LOG
static void log_line(const char *s) { am_util_stdio_printf("%s", s); }
#endif

void watch_gpu_init(void)
{
    /* Must run AFTER SRAM power config, BEFORE any MSPI/IOM DMA user.
     * Apollo4l HAL DMA entry points can indirectly call nema_init(). */
#if LV_USE_LOG
    lv_log_register_print_cb(log_line);
#endif
    lv_init();
    am_gpu_power_enable(AM_GPU_PWRCTRL_USER);
    if (nema_get_error() != NEMA_ERR_NO_ERROR || lv_mem_test() != LV_RES_OK) {
        am_util_stdio_printf("GPU/heap initialization failed.\r\n");
        configASSERT(0);
    }
    watch_gpu_ready = true;
    am_util_stdio_printf("Ambiq LVGL 8.1 GPU initialized; BGRA8888 -> RGB565 panel.\r\n");
}
