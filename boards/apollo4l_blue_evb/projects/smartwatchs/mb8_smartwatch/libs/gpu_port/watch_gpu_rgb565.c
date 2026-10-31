/* Experimental source-built LVGL 8.1 / Apollo4l NemaP RGB565 renderer.
 * No calls into lvgl_ambiq_porting.a: that archive has a 32-bit LVGL ABI.
 * The public LVGL hooks and real NemaGFX library are used instead. Unsupported
 * primitives are synchronized before returning LV_RES_INV to LVGL's CPU path.
 * Owned exclusively by the LVGL task; no destination/pointer survives a flush.
 */
#include "lvgl.h"
#include "lv_gpu_ambiq_nema.h"
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
#define SIMPLE_RESERVE 512u
#define ROUNDED_RESERVE (12u * 1024u)
static nema_cmdlist_t native_cl;
static uint32_t batch_glyphs;
static bool initialized, pending, label_active;

static void native_stop(const char *where, uint32_t error)
{
    am_util_stdio_printf("Native RGB565 stopped: %s error=%lu\r\n", where,
                         (unsigned long)error);
    configASSERT(0);
    for (;;) {}
}

void lv_gpu_ambiq_nema_wait(lv_disp_drv_t *driver)
{
    (void)driver;
    if (!initialized || !pending) return;
    configASSERT(nema_cl_get_bound() == NULL);
    uint32_t error = nema_get_error();
    if (error) native_stop("command build", error);
    /* Complete CPU writes before GPU texture/destination reads. */
    watch_gpu_cpu_write_complete();
    nema_cl_submit(&native_cl);
    int32_t id = native_cl.submission_id; /* wait() resets this field */
    error = nema_get_error();
    if (error) native_stop("submit", error);
    watch_gpu_note_submit(id, batch_glyphs);
    uint32_t start = watch_profile_start();
    int result = nema_cl_wait(&native_cl);
    WATCH_PROFILE_ADD(gpu_wait_cycles, start);
    error = nema_get_error();
    if (result || error) native_stop("completion", error ? error : (uint32_t)result);
    watch_gpu_note_completed(id, nema_reg_read(NEMA_CLID));
    pending = false;
    batch_glyphs = 0;
    nema_cl_rewind(&native_cl);
}

static lv_res_t cpu_fallback(void)
{
    lv_gpu_ambiq_nema_wait(NULL);
    ++watch_gpu_stats.native_cpu_fallbacks;
    return LV_RES_INV;
}

lv_res_t lv_gpu_ambiq_nema_init(void)
{
    configASSERT(!initialized);
    if (am_hal_pwrctrl_periph_enable(AM_HAL_PWRCTRL_PERIPH_GFX) != AM_HAL_STATUS_SUCCESS)
        native_stop("power", 1);
    int result = nema_init();
    uint32_t error = nema_get_error();
    if (result || error) native_stop("Nema init", error ? error : (uint32_t)result);
    native_cl = nema_cl_create_sized(NATIVE_CL_BYTES);
    error = nema_get_error();
    if (!native_cl.bo.base_virt || error) native_stop("CL allocation", error);
    initialized = true;
    return LV_RES_OK;
}

void lv_gpu_ambiq_nema_deinit(void)
{
    lv_gpu_ambiq_nema_wait(NULL);
    if (initialized) nema_cl_destroy(&native_cl);
    initialized = false;
    label_active = false;
}

static bool shared_range(const void *pointer, size_t size)
{
    uintptr_t address = (uintptr_t)pointer;
    return address >= 0x10060000u && address < 0x10160000u &&
           size <= 0x10160000u - address;
}

/* Clip coordinates are relative to this destination (not screen coordinates). */
static bool begin_target(lv_color_t *pixels, const lv_area_t *area,
                         const lv_area_t *clip, uint32_t reserve)
{
    int width = lv_area_get_width(area), height = lv_area_get_height(area);
    if (width <= 0 || height <= 0 || ((uintptr_t)pixels & 7u) ||
        ((unsigned)width * sizeof(*pixels) & 7u) ||
        !shared_range(pixels, (size_t)width * height * sizeof(*pixels))) return false;
    configASSERT(initialized && reserve <= NATIVE_CL_BYTES);
    configASSERT(nema_cl_get_bound() == NULL);
    /* SDK size/offset are uint32_t WORDS, not bytes (CL allocation is bytes).
     * Keep headroom for bind state and the submission trailer. */
    uint32_t reserve_words = (reserve + sizeof(uint32_t)-1u) / sizeof(uint32_t);
    configASSERT(native_cl.offset >= 0 && native_cl.offset <= native_cl.size);
    if ((uint32_t)(native_cl.size - native_cl.offset) < reserve_words)
        lv_gpu_ambiq_nema_wait(NULL);
    configASSERT((uint32_t)(native_cl.size - native_cl.offset) >= reserve_words);
    nema_cl_bind(&native_cl);
    nema_bind_dst_tex((uintptr_t)pixels, width, height, NEMA_RGB565,
                      width * sizeof(*pixels));
    nema_set_clip(clip->x1, clip->y1, lv_area_get_width(clip), lv_area_get_height(clip));
    return true;
}

static void end_commands(void)
{
    uint32_t error = nema_get_error();
    nema_cl_unbind();
    if (error) native_stop("record", error);
    pending = true;
}

static uint32_t rgba(lv_color_t color, lv_opa_t opacity)
{
    lv_color32_t expanded = { .full = lv_color_to32(color) };
    return nema_rgba(expanded.ch.red, expanded.ch.green, expanded.ch.blue, opacity);
}

static bool current_target(const lv_area_t *clip, uint32_t reserve, lv_area_t *area)
{
    lv_disp_t *display = _lv_refr_get_disp_refreshing();
    if (!display || display->driver->set_px_cb) return false;
    lv_disp_draw_buf_t *buffer = lv_disp_get_draw_buf(display);
    *area = buffer->area;
    lv_area_t clipped;
    if (!_lv_area_intersect(&clipped, area, clip)) return false;
    lv_area_move(&clipped, -area->x1, -area->y1);
    return begin_target(buffer->buf_act, area, &clipped, reserve);
}

lv_res_t lv_gpu_ambiq_nema_fill(const lv_area_t *disp_area, lv_color_t *disp_buf,
    const lv_area_t *draw_area, lv_color_t color, lv_opa_t opa,
    const lv_opa_t *mask, lv_draw_mask_res_t mask_res)
{
    (void)mask;
    if (mask_res == LV_DRAW_MASK_RES_TRANSP || opa == LV_OPA_TRANSP) return LV_RES_OK;
    /* Tiny masked scanlines and arbitrary masks retain the exact LVGL path. */
    if (mask_res != LV_DRAW_MASK_RES_FULL_COVER) return cpu_fallback();
    lv_area_t bounds = { 0, 0, lv_area_get_width(disp_area)-1, lv_area_get_height(disp_area)-1 };
    lv_area_t clip;
    if (!_lv_area_intersect(&clip, &bounds, draw_area)) return LV_RES_OK;
    if (!begin_target(disp_buf, disp_area, &clip, SIMPLE_RESERVE)) return cpu_fallback();
    nema_set_blend_fill(opa == LV_OPA_COVER ? NEMA_BL_SRC : NEMA_BL_SIMPLE);
    nema_fill_rect(clip.x1, clip.y1, lv_area_get_width(&clip), lv_area_get_height(&clip), rgba(color, opa));
    end_commands();
    ++watch_gpu_stats.native_fill_gpu;
    return LV_RES_OK;
}

lv_res_t lv_gpu_ambiq_nema_draw_bg(const lv_area_t *clip, const lv_area_t *draw,
    lv_coord_t radius, lv_color_t color, lv_opa_t opa, lv_blend_mode_t blend)
{
    if (blend != LV_BLEND_MODE_NORMAL || radius < 0 || radius > 128 ||
        lv_draw_mask_is_any(draw)) return cpu_fallback();
    lv_area_t visible;
    if (!_lv_area_intersect(&visible, clip, draw)) return LV_RES_OK;
    if (opa == LV_OPA_TRANSP) return LV_RES_OK;
    lv_area_t area;
    if (!current_target(&visible, radius ? ROUNDED_RESERVE : SIMPLE_RESERVE, &area))
        return cpu_fallback();
    /* AA rounded edges generate coverage alpha even for an opaque fill.
     * Match the vendor background path: source-alpha blend, not SRC replace. */
    nema_set_blend_fill(radius || opa != LV_OPA_COVER ? NEMA_BL_SIMPLE : NEMA_BL_SRC);
    int x = draw->x1 - area.x1, y = draw->y1 - area.y1;
    if (radius)
        nema_fill_rounded_rect_aa(x, y, lv_area_get_width(draw), lv_area_get_height(draw), radius, rgba(color, opa));
    else
        nema_fill_rect(x, y, lv_area_get_width(draw), lv_area_get_height(draw), rgba(color, opa));
    end_commands();
    ++watch_gpu_stats.native_bg_gpu;
    return LV_RES_OK;
}

lv_res_t lv_gpu_ambiq_nema_draw_label_pre(const lv_area_t *clip, lv_blend_mode_t blend)
{
    label_active = blend == LV_BLEND_MODE_NORMAL && !lv_draw_mask_is_any(clip);
    return label_active ? LV_RES_OK : cpu_fallback();
}
void lv_gpu_ambiq_nema_draw_label_post(void) { label_active = false; }
lv_res_t lv_gpu_ambiq_nema_draw_letter_pre(const lv_area_t *clip, lv_blend_mode_t blend)
{ return lv_gpu_ambiq_nema_draw_label_pre(clip, blend); }
void lv_gpu_ambiq_nema_draw_letter_post(void) { lv_gpu_ambiq_nema_draw_label_post(); }

lv_res_t lv_gpu_ambiq_nema_draw_letter(lv_coord_t x, lv_coord_t y,
    lv_font_glyph_dsc_t *glyph, const lv_area_t *clip, const uint8_t *bitmap,
    lv_color_t color, lv_opa_t opa, lv_blend_mode_t blend)
{
    if (!label_active || blend != LV_BLEND_MODE_NORMAL) goto fallback;
    const uint8_t *cached = watch_gpu_glyph_bitmap(bitmap, glyph->box_w, glyph->box_h, glyph->bpp);
    if (!cached) goto fallback;
    lv_area_t area;
    if (!current_target(clip, SIMPLE_RESERVE, &area)) goto fallback;
    uint32_t c = rgba(color, opa);
    /* Same public Nema A4 blend contract as the vendor glyph renderer:
     * alpha texture supplies coverage; constant alpha modulates label opacity. */
    nema_set_blend_blit(NEMA_BL_SIMPLE | (opa == LV_OPA_COVER ? 0u : NEMA_BLOP_MODULATE_A));
    nema_set_const_color(c);
    nema_set_tex_color(c);
    nema_bind_src_tex((uintptr_t)cached, glyph->box_w, glyph->box_h, NEMA_A4,
                      (glyph->box_w + 1u) / 2u, NEMA_FILTER_PS);
    nema_blit(x - area.x1, y - area.y1);
    end_commands();
    ++batch_glyphs;
    ++watch_gpu_stats.gpu_glyphs;
    return LV_RES_OK;
fallback:
    label_active = false;
    ++watch_gpu_stats.cpu_glyphs;
    return cpu_fallback();
}

/* Unsupported primitives return INV, never a false GPU success. LVGL's
 * software algorithms continue to use the native RGB565 fill hook where safe. */
lv_res_t lv_gpu_ambiq_nema_map(const lv_area_t *a, lv_color_t *b, const lv_area_t *c,
    const lv_area_t *d, const lv_color_t *e, lv_opa_t f, const lv_opa_t *g, lv_draw_mask_res_t h)
{ return cpu_fallback(); }
lv_res_t lv_gpu_ambiq_nema_blit(const lv_area_t *a, const lv_area_t *b,
    const void *c, const lv_draw_img_dsc_t *d) { return cpu_fallback(); }
lv_res_t lv_gpu_ambiq_nema_draw_bg_img(const lv_area_t *a, const lv_area_t *b,
    const lv_draw_rect_dsc_t *c, const lv_draw_img_dsc_t *d) { return cpu_fallback(); }
lv_res_t lv_gpu_ambiq_nema_draw_border(const lv_area_t *a, const lv_area_t *b,
    const lv_area_t *c, lv_coord_t d, lv_coord_t e, const lv_draw_rect_dsc_t *f) { return cpu_fallback(); }
lv_res_t lv_gpu_ambiq_nema_draw_outline(const lv_area_t *a, const lv_area_t *b,
    const lv_area_t *c, lv_coord_t d, lv_coord_t e, const lv_draw_rect_dsc_t *f) { return cpu_fallback(); }
lv_res_t lv_gpu_ambiq_nema_draw_line(const lv_point_t *a, const lv_point_t *b,
    const lv_area_t *c, const lv_draw_line_dsc_t *d) { return cpu_fallback(); }
lv_res_t lv_gpu_ambiq_nema_draw_polygon(const lv_point_t a[], uint16_t b,
    const lv_area_t *c, const lv_draw_rect_dsc_t *d) { return cpu_fallback(); }
lv_res_t lv_gpu_ambiq_nema_draw_arc(lv_coord_t a, lv_coord_t b, uint16_t c,
    uint16_t d, uint16_t e, const lv_area_t *f, const lv_draw_arc_dsc_t *g) { return cpu_fallback(); }
lv_res_t lv_gpu_ambiq_nema_gpu_only_fb(lv_disp_t *display)
{ (void)display; return LV_RES_INV; } /* RGB565 is CPU accessible. */
lv_res_t lv_gpu_ambiq_nema_fb_support_format(lv_img_cf_t cf)
{ return cf == LV_IMG_CF_TRUE_COLOR || cf == LV_IMG_CF_AMBIQ_RGB565 ? LV_RES_OK : LV_RES_INV; }
uint32_t lv_gpu_ambiq_nema_fb_size_needed(lv_img_cf_t cf, lv_coord_t width, lv_coord_t height)
{
    return lv_gpu_ambiq_nema_fb_support_format(cf) == LV_RES_OK && width > 0 && height > 0 ?
           (uint32_t)width * height * sizeof(lv_color_t) : 0;
}
lv_res_t lv_gpu_ambiq_nema_fb_set(void *descriptor, lv_img_cf_t cf, lv_coord_t w,
    lv_coord_t h, void *pixels, uint32_t size)
{
    uint32_t needed = lv_gpu_ambiq_nema_fb_size_needed(cf, w, h);
    if (!descriptor || !needed || size < needed) return LV_RES_INV;
    img_obj_t *fb = descriptor;
    memset(fb, 0, sizeof(*fb));
    fb->bo.base_phys = (uintptr_t)pixels;
    fb->bo.base_virt = pixels;
    fb->bo.size = size;
    fb->w = w; fb->h = h; fb->format = NEMA_RGB565; fb->stride = w * sizeof(lv_color_t);
    return LV_RES_OK;
}
lv_res_t lv_gpu_ambiq_nema_fb_clear(void *descriptor)
{
    img_obj_t *fb = descriptor;
    if (!fb || fb->format != NEMA_RGB565 || !fb->w || !fb->h) return cpu_fallback();
    lv_area_t area = {0, 0, fb->w-1, fb->h-1};
    if (!begin_target(fb->bo.base_virt, &area, &area, SIMPLE_RESERVE)) return cpu_fallback();
    nema_set_blend_fill(NEMA_BL_SRC);
    nema_fill_rect(0, 0, fb->w, fb->h, nema_rgba(0, 0, 0, 255));
    end_commands();
    return LV_RES_OK;
}
