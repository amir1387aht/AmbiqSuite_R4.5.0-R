#!/usr/bin/env python3
"""Ownership/order checks for double RGB565 staging; not a hardware benchmark."""
from pathlib import Path
import re
P=Path(__file__).resolve().parents[1]
code=(P/'src/helpers/display/lvgl_display.c').read_text()
panel=(P/'src/helpers/display/display.c').read_text()
assert 'xSemaphoreCreateBinary()' in code
assert 'xSemaphoreTake(panel_dma_complete, pdMS_TO_TICKS(1000))' in code
assert 'xSemaphoreGiveFromISR(panel_dma_complete, &higher_priority_task_woken)' in code
assert 'panel_pixels[display_width * display_height]' in code
assert 'memset(panel_pixels, 0, sizeof(panel_pixels));' in code
assert 'lv_mem_ssram_alloc(sizeof(panel_pixels) + 31u)' in code
assert '((uintptr_t)back_alloc + 31u) & ~(uintptr_t)31u' in code
flush=code[code.index('void disp_flush('):code.index('void disp_flush_done(')]
assert 'panel_drawing = panel_latest == panel_pixels ? panel_back_pixels : panel_pixels;' in flush
snapshot=flush.index('watch_gpu_copy_frame(panel_drawing, panel_latest, display_width, display_height)')
convert=flush.index('watch_gpu_convert_rect(')
last=flush.index('if (!lv_disp_flush_is_last(display))')
nonfinal=flush.index('lv_disp_flush_ready(display);',last)
wait=flush.index('wait_panel_idle();')
submit=flush.index('display_update_area(')
publish=flush.index('panel_latest = panel_drawing;')
ready=flush.rindex('lv_disp_flush_ready(display);')
assert snapshot < convert < last < nonfinal < wait < submit < publish < ready
assert flush.count('display_update_area(') == 1
assert '(uint8_t *)&panel_drawing[dirty_rows.first * display_width], disp_flush_done)' in flush
assert 'watch_dirty_rows_include(&dirty_rows' in flush
assert '.y1 = (lv_coord_t)dirty_rows.first' in flush and '.y2 = (lv_coord_t)dirty_rows.last' in flush
assert 'dirty_rows.valid = false;' in flush
callback=code[code.index('void disp_flush_done('):code.index('void touchpad_read(')]
assert 'lv_disp_flush_ready' not in callback, 'ISR must not release a newer LVGL flush'
assert '++watch_gpu_stats.panel_frames;' in callback
priority=int(re.search(r'NVIC_SetPriority\(mspi_display_interrupts\[.*?\], (0x[0-9a-fA-F]+)\)',panel)[1],16)
assert priority >= 4
assert '#define configMAX_SYSCALL_INTERRUPT_PRIORITY    (0x4 << 5)' in (P/'src/FreeRTOSConfig.h').read_text()
print('PASS: two aligned panel frames; copy previous before partial writes; never write MSPI-owned frame; wait before transfer-slot reuse; release LVGL after staging/queue; IRQ only signals DMA completion')
