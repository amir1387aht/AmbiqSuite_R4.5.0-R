/* Offload the measured CPU BGRA8888 -> RGB565 transport bottleneck.
 * The vendor LVGL archive still renders its unchanged 32-bit color ABI. */
#include "watch_gpu_convert.h"
#include "watch_framebuffer.h"
#include "watch_gpu.h"
#include "nema_cmdlist.h"
#include "nema_graphics.h"
#include "nema_blender.h"
#include "nema_error.h"
#include "nema_regs.h"
#include "FreeRTOS.h"
#include "am_util.h"
#if WATCH_GPU_CONVERT
static nema_cmdlist_t convert_cl;
static bool convert_available;
#endif

void watch_gpu_convert_init(void)
{
#if WATCH_GPU_CONVERT
    convert_cl = nema_cl_create_sized(1024);
    convert_available = convert_cl.bo.base_virt != NULL;
    uint32_t error = nema_get_error();
    if (error != NEMA_ERR_NO_ERROR) convert_available = false;
    if (!convert_available) {
        if (convert_cl.bo.base_virt) nema_cl_destroy(&convert_cl);
        am_util_stdio_printf("GPU conversion unavailable (%lu); using CPU fallback.\r\n",
                             (unsigned long)error);
    } else {
        am_util_stdio_printf("GPU conversion: Nema BGRA8888 -> RGB565 enabled.\r\n");
    }
#else
    am_util_stdio_printf("GPU conversion disabled: CPU reference path.\r\n");
#endif
}

#if WATCH_GPU_CONVERT
static bool watch_gpu_blit_rect(uint16_t *dst, size_t dst_stride,
    const void *src, size_t width, size_t height,
    nema_tex_format_t source_format, size_t source_pitch)
{
    if (!convert_available) return false;
    /* Reuse the CL only after its previous invocation finished. */
    configASSERT(nema_cl_get_bound() == NULL);
    nema_cl_rewind(&convert_cl);
    nema_cl_bind(&convert_cl);
    /* Nema texture bases must be 64-bit aligned. Even-column rounding only
     * ensures 4-byte RGB565 alignment. Clip past an aligned frame prefix. */
    uint32_t prefix = (uint32_t)(((uintptr_t)dst & 7u) / sizeof(*dst));
    uintptr_t aligned_dst = (uintptr_t)dst & ~(uintptr_t)7u;
    configASSERT(((uintptr_t)src & 7u) == 0);
    configASSERT(width + prefix <= dst_stride);
    nema_set_clip((int32_t)prefix, 0, (uint32_t)width, (uint32_t)height);
    nema_bind_dst_tex(aligned_dst, (uint32_t)(width + prefix), (uint32_t)height,
                      NEMA_RGB565, (int32_t)(dst_stride * sizeof(*dst)));
    nema_bind_src_tex((uintptr_t)src, (uint32_t)width, (uint32_t)height,
                      source_format, (int32_t)source_pitch, NEMA_FILTER_PS);
    nema_set_blend_blit(NEMA_BL_SRC);
    nema_blit((int)prefix, 0);
    uint32_t error = nema_get_error();
    nema_cl_unbind();
    if (error == NEMA_ERR_NO_ERROR) {
        nema_cl_submit(&convert_cl);
        error = nema_get_error();
        /* SDK nema_cl_wait resets this field to -1 on completion. */
        int32_t submission_id = convert_cl.submission_id;
        int wait_result = nema_cl_wait(&convert_cl);
        if (error != NEMA_ERR_NO_ERROR || wait_result != 0) {
            /* Never let MSPI read an incomplete GPU destination. */
            am_util_stdio_printf("GPU conversion failed (%lu/%d); stopped.\r\n",
                                 (unsigned long)error, wait_result);
            configASSERT(0);
            for (;;) {}
        }
        watch_gpu_note_submit(submission_id, 0);
        watch_gpu_note_completed(submission_id, nema_reg_read(NEMA_CLID));
        return true;
    }
    /* No CL was submitted: safe to fall back on CPU after prior draws finished. */
    convert_available = false;
    nema_cl_destroy(&convert_cl);
    am_util_stdio_printf("GPU conversion command error (%lu); CPU fallback.\r\n",
                         (unsigned long)error);
    return false;
}
#endif

void watch_gpu_convert_rect(uint16_t *dst, size_t dst_stride,
                            const void *src, size_t width, size_t height)
{
    configASSERT(dst && src && width && height && dst_stride >= width);
#if WATCH_GPU_CONVERT
    if (watch_gpu_blit_rect(dst, dst_stride, src, width, height,
                           NEMA_BGRA8888, width * sizeof(uint32_t))) {
        ++watch_gpu_stats.convert_gpu;
        return;
    }
#endif
    watch_bgra8888_rect_to_rgb565(dst, dst_stride, src, width, height);
    watch_gpu_cpu_write_complete();
    ++watch_gpu_stats.convert_cpu;
}

void watch_gpu_copy_frame(uint16_t *dst, const uint16_t *src,
                          size_t width, size_t height)
{
    configASSERT(dst && src && dst != src && width && height);
    uint32_t start = watch_profile_start();
#if WATCH_GPU_CONVERT
    if (!watch_gpu_blit_rect(dst, width, src, width, height,
                            NEMA_RGB565, width * sizeof(uint16_t)))
#endif
    {
        memcpy(dst, src, width * height * sizeof(*dst));
        watch_gpu_cpu_write_complete();
    }
    WATCH_PROFILE_ADD(frame_copy_cycles, start);
}
