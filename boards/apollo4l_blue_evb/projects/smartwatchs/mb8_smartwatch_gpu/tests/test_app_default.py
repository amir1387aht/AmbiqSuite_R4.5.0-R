#!/usr/bin/env python3
"""Check the original app path and all non-port LVGL settings are preserved."""
from pathlib import Path
import re
P = Path(__file__).resolve().parents[1]
O = P.with_name('mb8_smartwatch')
count = 0
for folder in ['src/screens/menu', 'src/sensors/accl']:
    for f in (O/folder).rglob('*'):
        if f.is_file() and f.suffix in ['.c', '.h']:
            assert f.read_bytes() == (P/f.relative_to(O)).read_bytes(), f
            count += 1
assert (P/'src/main.c').read_bytes() == (O/'src/main.c').read_bytes()
def defines(s):
    return dict(re.findall(r'^\s*#define\s+(LV_\w+)\s+([^\n]+)', s, re.M))
a = defines((O/'src/lv_conf.h').read_text())
b = defines((P/'src/lv_conf.h').read_text())
for key in a:
    if key.startswith('LV_FONT_') or key in ['LV_USE_PERF_MONITOR', 'LV_USE_PERF_MONITOR_POS', 'LV_USE_MEM_MONITOR', 'LV_USE_LOG']:
        assert b[key] == a[key], (key, a[key], b.get(key))
assert b['LV_USE_PERF_MONITOR'] == '1'
assert b['LV_USE_PERF_MONITOR_POS'] == 'LV_ALIGN_BOTTOM_MID'
screen = (P/'src/helpers/display/lvgl_display.c').read_text()
assert '    screen_menu_create();' in screen
assert '//ui_init();' in screen
assert 'xTaskCreate(lightSens_task' in (P/'src/rtos.c').read_text()
assert 'xTaskCreate(acclSens_task' in (P/'src/rtos.c').read_text()
for folder in ['src', 'libs/gpu_port', 'gcc']:
    for f in (P/folder).rglob('*'):
        if f.is_file() and (f.suffix in ['.c', '.h', '.bat'] or f.name == 'Makefile'):
            assert 'smoke' not in f.read_text().lower(), f
print(f'PASS: original app/menu, {count} menu/accelerometer source files, all original font options, bottom-middle FPS/CPU monitor and logging settings preserved')
