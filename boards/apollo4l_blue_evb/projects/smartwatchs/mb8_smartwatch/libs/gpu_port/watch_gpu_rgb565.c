/* Apollo4l NemaP renderer for the upstream LVGL 8.4 draw-context API.
 * RGB565 throughout: no conversion buffer or proprietary LVGL 8.1 archive.
 * GPU commands are owned by the UI task. Every software operation, buffer
 * change and release is fenced; software primitives cannot enqueue GPU work.
 */
#include "lvgl.h"
#include "watch_gpu_renderer.h"
#include "watch_gpu.h"
#include "watch_glyph_cache.h"
#include "nema_graphics.h"
#include "nema_provisional.h"
#include "nema_blender.h"
#include "nema_cmdlist.h"
#include "nema_error.h"
#include "nema_regs.h"
#include "nema_sys_defs.h"
#include "am_mcu_apollo.h"
#include "am_util.h"
#include "FreeRTOS.h"
#include <string.h>

#if !WATCH_NATIVE_RGB565 || LV_COLOR_DEPTH != 16 || LV_COLOR_16_SWAP
#error "Native renderer requires unswapped LVGL RGB565"
#endif
_Static_assert(sizeof(lv_color_t) == 2, "RGB565 ABI required");
#define NATIVE_CL_BYTES (16u * 1024u)
#define SIMPLE_RESERVE  512u
#define ROUNDED_RESERVE (12u * 1024u)
static nema_cmdlist_t native_command_list;
static uint32_t       batch_glyph_count;
static bool           initialized, commands_pending;

static void native_stop(const char *where, uint32_t error)
{
    am_util_stdio_printf("Native RGB565 stopped: %s error=%lu\r\n", where, (unsigned long)error);
    configASSERT(0);
    for (;;)
    {
    }
}

void watch_gpu_renderer_wait(void)
{
    if (!initialized || !commands_pending)
        return;

    configASSERT(nema_cl_get_bound() == NULL);
    uint32_t error = nema_get_error();
    if (error)
        native_stop("command build", error);

    /* Complete CPU writes before GPU texture/destination reads. */
    watch_gpu_cpu_write_complete();
    nema_cl_submit(&native_command_list);
    int32_t submission_id = native_command_list.submission_id; /* wait() resets this field */
    error                 = nema_get_error();
    if (error)
        native_stop("submit", error);
    watch_gpu_note_submit(submission_id, batch_glyph_count);

    uint32_t start  = watch_profile_start();
    int      result = nema_cl_wait(&native_command_list);
    WATCH_PROFILE_ADD(gpu_wait_cycles, start);
    error = nema_get_error();
    if (result || error)
        native_stop("completion", error ? error : (uint32_t)result);

    watch_gpu_note_completed(submission_id, nema_reg_read(NEMA_CLID));
    commands_pending  = false;
    batch_glyph_count = 0;
    nema_cl_rewind(&native_command_list);
}

void watch_gpu_renderer_init(void)
{
    configASSERT(!initialized);
    if (am_hal_pwrctrl_periph_enable(AM_HAL_PWRCTRL_PERIPH_GFX) != AM_HAL_STATUS_SUCCESS)
        native_stop("power", 1);
    int      result = nema_init();
    uint32_t error  = nema_get_error();
    if (result || error)
        native_stop("Nema init", error ? error : (uint32_t)result);
    native_command_list = nema_cl_create_sized(NATIVE_CL_BYTES);
    error               = nema_get_error();
    if (!native_command_list.bo.base_virt || error)
        native_stop("CL allocation", error);
    initialized = true;
}

static bool is_shared_memory_range(const void *pointer, size_t size)
{
    uintptr_t address = (uintptr_t)pointer;
    return address >= 0x10060000u && address < 0x10160000u && size <= 0x10160000u - address;
}

/* Clip coordinates are relative to this destination (not screen coordinates). */
static bool begin_target(
    lv_color_t *pixels, const lv_area_t *area, const lv_area_t *clip, uint32_t reserved_bytes)
{
    int width = lv_area_get_width(area), height = lv_area_get_height(area);
    if (width <= 0 || height <= 0 || ((uintptr_t)pixels & 7u) ||
        ((unsigned)width * sizeof(*pixels) & 7u) ||
        !is_shared_memory_range(pixels, (size_t)width * height * sizeof(*pixels)))
        return false;
    configASSERT(initialized && reserved_bytes <= NATIVE_CL_BYTES);
    configASSERT(nema_cl_get_bound() == NULL);
    /* SDK size/offset are uint32_t WORDS, not bytes (CL allocation is bytes).
     * Keep headroom for bind state and the submission trailer. */
    uint32_t reserve_words = (reserved_bytes + sizeof(uint32_t) - 1u) / sizeof(uint32_t);
    configASSERT(
        native_command_list.offset >= 0 && native_command_list.offset <= native_command_list.size);
    if ((uint32_t)(native_command_list.size - native_command_list.offset) < reserve_words)
        watch_gpu_renderer_wait();
    configASSERT(
        (uint32_t)(native_command_list.size - native_command_list.offset) >= reserve_words);
    nema_cl_bind(&native_command_list);
    nema_bind_dst_tex((uintptr_t)pixels, width, height, NEMA_RGB565, width * sizeof(*pixels));
    nema_set_clip(clip->x1, clip->y1, lv_area_get_width(clip), lv_area_get_height(clip));
    return true;
}

static void end_commands(void)
{
    uint32_t error = nema_get_error();
    nema_cl_unbind();
    if (error)
        native_stop("record", error);
    commands_pending = true;
}

static uint32_t color_to_nema_rgba(lv_color_t color, lv_opa_t opacity)
{
    lv_color32_t expanded = {.full = lv_color_to32(color)};
    return nema_rgba(expanded.ch.red, expanded.ch.green, expanded.ch.blue, opacity);
}

/* 8.4 passes the active destination in the context, including temporary
 * layers. Never infer it from the display's primary draw buffer. */
static bool current_target(
    lv_draw_ctx_t *ctx, const lv_area_t *clip, uint32_t reserved_bytes, lv_area_t *area)
{
    watch_gpu_draw_ctx_t *gpu = (watch_gpu_draw_ctx_t *)ctx;
    if (gpu->software_depth || gpu->display_driver->screen_transp ||
        gpu->display_driver->set_px_cb || !ctx->buf || !ctx->buf_area || !ctx->clip_area)
        return false;
    *area = *ctx->buf_area;
    lv_area_t clipped;
    if (!_lv_area_intersect(&clipped, clip, ctx->clip_area) ||
        !_lv_area_intersect(&clipped, &clipped, area))
        return false;
    lv_area_move(&clipped, -area->x1, -area->y1);
    return begin_target(ctx->buf, area, &clipped, reserved_bytes);
}

static void software_begin(lv_draw_ctx_t *ctx)
{
    watch_gpu_renderer_wait();
    watch_gpu_draw_ctx_t *gpu = (watch_gpu_draw_ctx_t *)ctx;
    if (!gpu->software_depth++)
        ++watch_gpu_stats.native_cpu_fallbacks;
}

static void software_end(lv_draw_ctx_t *ctx)
{
    watch_gpu_draw_ctx_t *gpu = (watch_gpu_draw_ctx_t *)ctx;
    configASSERT(gpu->software_depth);
    --gpu->software_depth;
    watch_gpu_cpu_write_complete();
}

static lv_opa_t normalized_opacity(lv_opa_t opa)
{
    return opa >= LV_OPA_MAX ? LV_OPA_COVER : opa;
}

static void context_wait(lv_draw_ctx_t *ctx)
{
    (void)ctx;
    watch_gpu_renderer_wait();
}

static void blend(lv_draw_ctx_t *ctx, const lv_draw_sw_blend_dsc_t *dsc)
{
    if (dsc->opa <= LV_OPA_MIN || (dsc->mask_buf && dsc->mask_res == LV_DRAW_MASK_RES_TRANSP))
        return;
    lv_area_t visible, area;
    if (!_lv_area_intersect(&visible, dsc->blend_area, ctx->clip_area))
        return;
    if (!dsc->src_buf && dsc->blend_mode == LV_BLEND_MODE_NORMAL &&
        (!dsc->mask_buf || dsc->mask_res == LV_DRAW_MASK_RES_FULL_COVER) &&
        current_target(ctx, &visible, SIMPLE_RESERVE, &area))
    {
        lv_opa_t opa = normalized_opacity(dsc->opa);
        nema_set_blend_fill(opa == LV_OPA_COVER ? NEMA_BL_SRC : NEMA_BL_SIMPLE);
        nema_fill_rect(visible.x1 - area.x1,
            visible.y1 - area.y1,
            lv_area_get_width(&visible),
            lv_area_get_height(&visible),
            color_to_nema_rgba(dsc->color, opa));
        end_commands();
        ++watch_gpu_stats.native_fill_gpu;
        return;
    }
    /* A software primitive may alternate direct pixel writes with blend calls.
     * Keep its entire lifetime synchronous, not just individual scanlines. */
    if (((watch_gpu_draw_ctx_t *)ctx)->software_depth)
        lv_draw_sw_blend_basic(ctx, dsc);
    else
    {
        software_begin(ctx);
        lv_draw_sw_blend_basic(ctx, dsc);
        software_end(ctx);
    }
}

static bool draw_background(
    lv_draw_ctx_t *ctx, const lv_draw_rect_dsc_t *dsc, const lv_area_t *coords)
{
    if (dsc->blend_mode != LV_BLEND_MODE_NORMAL || dsc->bg_grad.dir != LV_GRAD_DIR_NONE ||
        (dsc->bg_img_src && dsc->bg_img_opa > LV_OPA_MIN) || lv_draw_mask_is_any(coords))
        return false;
    lv_area_t visible;
    if (dsc->bg_opa <= LV_OPA_MIN || !_lv_area_intersect(&visible, coords, ctx->clip_area))
        return true;
    lv_coord_t radius     = dsc->radius;
    int        max_radius = LV_MIN(lv_area_get_width(coords), lv_area_get_height(coords)) / 2;
    if (radius > max_radius)
        radius = max_radius; /* Includes LV_RADIUS_CIRCLE, as in software. */
    if (radius < 0 || radius > 128 ||
        (radius && !((watch_gpu_draw_ctx_t *)ctx)->display_driver->antialiasing) ||
        (dsc->radius && dsc->border_width > 1 && dsc->border_opa >= LV_OPA_MAX))
        return false;
    lv_area_t area;
    if (!current_target(ctx, &visible, radius ? ROUNDED_RESERVE : SIMPLE_RESERVE, &area))
        return false;
    lv_opa_t opa = normalized_opacity(dsc->bg_opa);
    /* Coverage alpha is needed at AA rounded edges even for opaque colors. */
    nema_set_blend_fill(radius || opa != LV_OPA_COVER ? NEMA_BL_SIMPLE : NEMA_BL_SRC);
    if (radius)
        nema_fill_rounded_rect_aa(coords->x1 - area.x1,
            coords->y1 - area.y1,
            lv_area_get_width(coords),
            lv_area_get_height(coords),
            radius,
            color_to_nema_rgba(dsc->bg_color, opa));
    else
        nema_fill_rect(coords->x1 - area.x1,
            coords->y1 - area.y1,
            lv_area_get_width(coords),
            lv_area_get_height(coords),
            color_to_nema_rgba(dsc->bg_color, opa));
    end_commands();
    ++watch_gpu_stats.native_bg_gpu;
    return true;
}

static void draw_rect(lv_draw_ctx_t *ctx, const lv_draw_rect_dsc_t *dsc, const lv_area_t *coords)
{
    if ((!dsc->border_width || dsc->border_opa <= LV_OPA_MIN) &&
        (!dsc->outline_width || dsc->outline_opa <= LV_OPA_MIN) &&
        (!dsc->shadow_width || dsc->shadow_opa <= LV_OPA_MIN) && draw_background(ctx, dsc, coords))
        return;
    software_begin(ctx);
    lv_draw_sw_rect(ctx, dsc, coords);
    software_end(ctx);
}

static void draw_bg(lv_draw_ctx_t *ctx, const lv_draw_rect_dsc_t *dsc, const lv_area_t *coords)
{
    if (draw_background(ctx, dsc, coords))
        return;
    software_begin(ctx);
    lv_draw_sw_bg(ctx, dsc, coords);
    software_end(ctx);
}

static void draw_letter(
    lv_draw_ctx_t *ctx, const lv_draw_label_dsc_t *dsc, const lv_point_t *position, uint32_t letter)
{
    if (dsc->opa <= LV_OPA_MIN)
        return;
    lv_font_glyph_dsc_t glyph;
    if (!dsc->font || dsc->blend_mode != LV_BLEND_MODE_NORMAL ||
        !lv_font_get_glyph_dsc(dsc->font, &glyph, letter, 0) || glyph.is_placeholder ||
        !glyph.resolved_font || glyph.resolved_font->subpx || lv_draw_mask_is_any(ctx->clip_area))
        goto fallback;
    if (!glyph.box_w || !glyph.box_h)
        return;
    int x = position->x + glyph.ofs_x;
    int y = position->y + dsc->font->line_height - dsc->font->base_line - glyph.box_h - glyph.ofs_y;
    lv_area_t bounds = {x, y, x + glyph.box_w - 1, y + glyph.box_h - 1};
    lv_area_t visible, area;
    if (!_lv_area_intersect(&visible, &bounds, ctx->clip_area))
        return;
    const uint8_t *bitmap = lv_font_get_glyph_bitmap(glyph.resolved_font, letter);
    const uint8_t *cached = watch_gpu_glyph_bitmap(bitmap, glyph.box_w, glyph.box_h, glyph.bpp);
    if (!cached || !current_target(ctx, &visible, SIMPLE_RESERVE, &area))
        goto fallback;
    lv_opa_t opa   = normalized_opacity(dsc->opa);
    uint32_t color = color_to_nema_rgba(dsc->color, opa);
    nema_set_blend_blit(NEMA_BL_SIMPLE | (opa == LV_OPA_COVER ? 0u : NEMA_BLOP_MODULATE_A));
    nema_set_const_color(color);
    nema_set_tex_color(color);
    nema_bind_src_tex((uintptr_t)cached,
        glyph.box_w,
        glyph.box_h,
        NEMA_A4,
        (glyph.box_w + 1u) / 2u,
        NEMA_FILTER_PS);
    nema_blit(x - area.x1, y - area.y1);
    end_commands();
    ++batch_glyph_count;
    ++watch_gpu_stats.gpu_glyphs;
    return;
fallback:
    ++watch_gpu_stats.cpu_glyphs;
    software_begin(ctx);
    lv_draw_sw_letter(ctx, dsc, position, letter);
    software_end(ctx);
}

/* Fence all software entry points, including those which can write pixels
 * directly rather than invoking the blend callback. */
static void draw_img_decoded(lv_draw_ctx_t *ctx,
    const lv_draw_img_dsc_t                *dsc,
    const lv_area_t                        *coords,
    const uint8_t                          *pixels,
    lv_img_cf_t                             format)
{
    software_begin(ctx);
    lv_draw_sw_img_decoded(ctx, dsc, coords, pixels, format);
    software_end(ctx);
}

static void draw_line(lv_draw_ctx_t *ctx,
    const lv_draw_line_dsc_t        *dsc,
    const lv_point_t                *first,
    const lv_point_t                *last)
{
    software_begin(ctx);
    lv_draw_sw_line(ctx, dsc, first, last);
    software_end(ctx);
}

static void draw_arc(lv_draw_ctx_t *ctx,
    const lv_draw_arc_dsc_t        *dsc,
    const lv_point_t               *center,
    uint16_t                        radius,
    uint16_t                        start,
    uint16_t                        end)
{
    software_begin(ctx);
    lv_draw_sw_arc(ctx, dsc, center, radius, start, end);
    software_end(ctx);
}

static void draw_polygon(
    lv_draw_ctx_t *ctx, const lv_draw_rect_dsc_t *dsc, const lv_point_t *points, uint16_t count)
{
    software_begin(ctx);
    lv_draw_sw_polygon(ctx, dsc, points, count);
    software_end(ctx);
}

static void draw_transform(lv_draw_ctx_t *ctx,
    const lv_area_t                      *area,
    const void                           *source,
    lv_coord_t                            width,
    lv_coord_t                            height,
    lv_coord_t                            stride,
    const lv_draw_img_dsc_t              *dsc,
    lv_img_cf_t                           format,
    lv_color_t                           *colors,
    lv_opa_t                             *opacity)
{
    software_begin(ctx);
    lv_draw_sw_transform(ctx, area, source, width, height, stride, dsc, format, colors, opacity);
    software_end(ctx);
}

static void buffer_copy(lv_draw_ctx_t *ctx,
    void                              *dest,
    lv_coord_t                         dest_stride,
    const lv_area_t                   *dest_area,
    void                              *source,
    lv_coord_t                         source_stride,
    const lv_area_t                   *source_area)
{
    software_begin(ctx);
    lv_draw_sw_buffer_copy(ctx, dest, dest_stride, dest_area, source, source_stride, source_area);
    software_end(ctx);
}

static lv_draw_layer_ctx_t *layer_init(
    lv_draw_ctx_t *ctx, lv_draw_layer_ctx_t *layer, lv_draw_layer_flags_t flags)
{
    software_begin(ctx);
    lv_draw_layer_ctx_t *result = lv_draw_sw_layer_create(ctx, layer, flags);
    software_end(ctx);
    return result;
}

static void layer_adjust(
    lv_draw_ctx_t *ctx, lv_draw_layer_ctx_t *layer, lv_draw_layer_flags_t flags)
{
    software_begin(ctx);
    lv_draw_sw_layer_adjust(ctx, layer, flags);
    software_end(ctx);
}

static void layer_blend(
    lv_draw_ctx_t *ctx, lv_draw_layer_ctx_t *layer, const lv_draw_img_dsc_t *dsc)
{
    software_begin(ctx);
    lv_draw_sw_layer_blend(ctx, layer, dsc);
    software_end(ctx);
}

static void layer_destroy(lv_draw_ctx_t *ctx, lv_draw_layer_ctx_t *layer)
{
    software_begin(ctx);
    lv_draw_sw_layer_destroy(ctx, layer);
    software_end(ctx);
}

void watch_gpu_draw_ctx_init(lv_disp_drv_t *driver, lv_draw_ctx_t *ctx)
{
    lv_draw_sw_init_ctx(driver, ctx);
    watch_gpu_draw_ctx_t *gpu = (watch_gpu_draw_ctx_t *)ctx;
    gpu->display_driver       = driver;
    gpu->software_depth       = 0;
    gpu->software.blend       = blend;
    ctx->draw_rect            = draw_rect;
    ctx->draw_bg              = draw_bg;
    ctx->draw_letter          = draw_letter;
    ctx->draw_img_decoded     = draw_img_decoded;
    ctx->draw_line            = draw_line;
    ctx->draw_arc             = draw_arc;
    ctx->draw_polygon         = draw_polygon;
    ctx->draw_transform       = draw_transform;
    ctx->buffer_copy          = buffer_copy;
    ctx->wait_for_finish      = context_wait;
    ctx->layer_init           = layer_init;
    ctx->layer_adjust         = layer_adjust;
    ctx->layer_blend          = layer_blend;
    ctx->layer_destroy        = layer_destroy;
}

void watch_gpu_draw_ctx_deinit(lv_disp_drv_t *driver, lv_draw_ctx_t *ctx)
{
    watch_gpu_renderer_wait();
    lv_draw_sw_deinit_ctx(driver, ctx);
}
