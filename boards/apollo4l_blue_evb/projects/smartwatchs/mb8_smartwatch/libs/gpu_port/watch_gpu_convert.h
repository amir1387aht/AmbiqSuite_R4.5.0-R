#ifndef WATCH_GPU_CONVERT_H
#define WATCH_GPU_CONVERT_H
#include <stddef.h>
#include <stdint.h>
#ifndef WATCH_GPU_CONVERT
#define WATCH_GPU_CONVERT 1
#endif
void watch_gpu_convert_init(void);
/* Called only by the LVGL task after all rendering CLs have completed.
 * Source is 64-bit aligned; destination is within a 64-bit-aligned RGB565
 * frame with a stride divisible by four pixels (the watch uses 192).
 * Returns only when both the source buffer and panel destination can be reused. */
void watch_gpu_convert_rect(uint16_t *dst, size_t dst_stride,
                            const void *src, size_t width, size_t height);
/* Snapshot the last submitted RGB565 frame before a partial refresh. Both
 * GPU and MSPI may read src concurrently, but only the GPU/CPU writes dst. */
void watch_gpu_copy_frame(uint16_t *dst, const uint16_t *src,
                          size_t width, size_t height);
#endif
