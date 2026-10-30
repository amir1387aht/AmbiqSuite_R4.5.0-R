#!/usr/bin/env python3
from pathlib import Path
import re
P = Path(__file__).resolve().parents[1]
backend = (P/'libs/gpu_port/lv_gpu_ambiq_nema.c').read_text()
letter = backend[backend.index('lv_res_t lv_gpu_ambiq_nema_draw_letter(lv_coord_t'):backend.index('lv_res_t lv_gpu_ambiq_nema_draw_border(')]
assert '#if !LV_AMBIQ_FONT_REALIGN_BITMAP' not in backend
assert 'watch_gpu_glyph_bitmap(map_p, g->box_w, g->box_h, g->bpp)' in letter
fallback = letter[letter.index('if (gpu_bitmap == NULL)'):letter.index('map_p = gpu_bitmap;')]
assert fallback.index('lv_gpu_ambiq_nema_cl_submit_link(pLabelCL)') < fallback.index('lv_gpu_ambiq_nema_wait(NULL)') < fallback.index('return LV_RES_INV;')
assert '++watch_gpu_stats.gpu_glyphs;' in letter
assert 'lv_area_get_size(draw_area) <= 256' in backend
assert 'watch_gpu_note_completed(cl->submission_id, nema_reg_read(NEMA_CLID))' in backend
config = (P/'src/lv_conf.h').read_text()
assert re.search(r'#define LV_USE_PERF_MONITOR\s+1\b', config)
assert re.search(r'#define LV_USE_GPU_AMBIQ_NEMA\s+1\b', config)
assert re.search(r'#define LV_AMBIQ_FONT_REALIGN_BITMAP\s+0\b', config)
assert re.search(r'#define LV_DISP_DEF_REFR_PERIOD\s+16\b', config)
app = (P/'src/helpers/display/lvgl_display.c').read_text()
assert 'uint32_t next_timer_ms = lv_timer_handler();' in app
assert 'vTaskDelay(delay_ticks ? delay_ticks : 1);' in app
cache = (P/'libs/gpu_port/watch_glyph_cache.c').read_text()
assert 'lv_mem_ssram_alloc(WATCH_GLYPH_ARENA_BYTES)' in cache
assert 'start < 0x00018000u' in cache and 'start >= 0x00200000u' in cache
assert 'AM_HAL_DAXI_CONTROL_FLUSH' in cache
print('PASS: direct cached GPU glyph path; ordered CPU fallback; tiny-mask threshold; hardware completion hook; FPS monitor and 16ms refresh retained')
