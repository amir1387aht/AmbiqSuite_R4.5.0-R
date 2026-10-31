/* SDK font data reused unchanged; reconcile the SDK public descriptor qualifier. */
#include "lvgl.h"
#if LV_FONT_MONTSERRAT_34
#define lv_font_montserrat_34 watch_sdk_const_montserrat_34
#include "third_party/LVGL/lvgl/src/font/lv_font_montserrat_34.c"
#undef lv_font_montserrat_34
lv_font_t lv_font_montserrat_34 = {
    .get_glyph_dsc = lv_font_get_glyph_dsc_fmt_txt,    /*Function pointer to get glyph's data*/
    .get_glyph_bitmap = lv_font_get_bitmap_fmt_txt,    /*Function pointer to get glyph's bitmap*/
    .line_height = 38,          /*The maximum line height required by the font*/
    .base_line = 7,             /*Baseline measured from the bottom of the line*/
#if !(LVGL_VERSION_MAJOR == 6 && LVGL_VERSION_MINOR == 0)
    .subpx = LV_FONT_SUBPX_NONE,
#endif
#if LV_VERSION_CHECK(7, 4, 0)
    .underline_position = -3,
    .underline_thickness = 2,
#endif
    .dsc = &font_dsc           /*The custom font data. Will be accessed by `get_glyph_bitmap/dsc` */
};
#endif
