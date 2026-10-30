#!/usr/bin/env python3
"""Integration structure checks; not a hardware performance measurement."""
from pathlib import Path
import re
P = Path(__file__).resolve().parents[1]
code = (P/'src/helpers/display/lvgl_display.c').read_text()
panel = (P/'src/helpers/display/display.c').read_text()
assert 'disp_drv.wait_cb = disp_wait;' in code
assert 'xSemaphoreTake(panel_dma_complete, pdMS_TO_TICKS(1000))' in code
assert 'xSemaphoreCreateBinary()' in code
assert 'xSemaphoreGiveFromISR(panel_dma_complete, &higher_priority_task_woken)' in code
assert 'BaseType_t higher_priority_task_woken = pdFALSE;' in code
assert 'panel_pixels[display_width * display_height]' in code
assert 'memset(panel_pixels, 0, sizeof(panel_pixels));' in code
flush = code[code.index('void disp_flush('):code.index('void disp_flush_done(')]
copy = flush.index('watch_bgra8888_rect_to_rgb565(')
last = flush.index('if (!lv_disp_flush_is_last(display))')
ready = flush.index('lv_disp_flush_ready(display);', last)
ret = flush.index('return;', ready)
submit = flush.index('display_update_area(')
assert copy < last < ready < ret < submit
assert flush.count('display_update_area(') == 1
assert '(uint8_t *)&panel_pixels[dirty_rows.first * display_width], disp_flush_done)' in flush
assert 'watch_dirty_rows_include(&dirty_rows' in flush
assert '.y1 = (lv_coord_t)dirty_rows.first' in flush and '.y2 = (lv_coord_t)dirty_rows.last' in flush
assert 'dirty_rows.valid = false;' in flush
assert 'lv_disp_flush_ready' not in flush[submit:]
priority = int(re.search(r'NVIC_SetPriority\(mspi_display_interrupts\[.*?\], (0x[0-9a-fA-F]+)\)', panel)[1], 16)
assert priority >= 4
config = (P/'src/FreeRTOSConfig.h').read_text()
assert '#define configMAX_SYSCALL_INTERRUPT_PRIORITY    (0x4 << 5)' in config
print('PASS: partial flushes stage pixels; only last flush submits bounded panel row range; DMA ISR releases buffer/semaphore; priority permits RTOS calls')
print('Static integration checks only; on-device FPS, CPU load and tearing remain unmeasured')
