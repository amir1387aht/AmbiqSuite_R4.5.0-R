#!/usr/bin/env python3
"""Structural checks only; actual GPU color output and speed need the watch."""
from pathlib import Path
P=Path(__file__).resolve().parents[1]
s=(P/'libs/gpu_port/watch_gpu_convert.c').read_text()
assert s.index('int32_t submission_id = convert_cl.submission_id;') < s.index('nema_cl_wait(&convert_cl)') < s.index('watch_gpu_note_completed(submission_id,')
assert 'watch_gpu_cpu_write_complete();' in s
for x in ['nema_cl_create_sized(1024)', 'nema_cl_rewind(&convert_cl)', 'NEMA_RGB565', 'NEMA_BGRA8888', 'NEMA_BL_SRC', 'NEMA_FILTER_PS', 'dst_stride * sizeof(*dst)', 'width * sizeof(uint32_t)', 'nema_cl_unbind()', 'watch_bgra8888_rect_to_rgb565(dst, dst_stride, src, width, height)']:
    assert x in s, x
assert 'nema_cl_get_bound() == NULL' in s
app=(P/'src/helpers/display/lvgl_display.c').read_text()
assert app.index('lv_gpu_ambiq_nema_wait(display);') < app.index('watch_gpu_convert_rect(') < app.index('display_update_area(')
m=(P/'gcc/Makefile').read_text()
assert 'GPU_CONVERT ?= 1' in m and '-DWATCH_GPU_CONVERT=$(GPU_CONVERT)' in m
assert "GPU_CONVERT=$(GPU_CONVERT) TOOLCHAIN=" in m
h=(P/'libs/gpu_port/watch_gpu.h').read_text()
for x in ['convert_gpu', 'convert_cpu', 'te_wait_cycles', 'panel_dma_cycles']: assert x in h
print('PASS: GPU format/stride/copy commands, blocking completion before source reuse and MSPI, CPU fallback, build toggle and timing counters')
