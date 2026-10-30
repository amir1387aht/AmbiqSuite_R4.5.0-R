#!/usr/bin/env python3
"""Verify plain make selects the original watch app and menu files are unchanged."""
from pathlib import Path
import os
import subprocess
import tempfile
P = Path(__file__).resolve().parents[1]
O = P.with_name('mb8_smartwatch')
with tempfile.TemporaryDirectory() as d:
    f = Path(d) / 'default.mk'
    f.write_text('.PHONY: app-default\napp-default:\n\t$(info MODE=$(GPU_SMOKE_TEST))\n')
    result = subprocess.check_output([os.environ.get('MAKE', 'make'), '-s',
        '--no-print-directory', '-f', 'Makefile', '-f', str(f),
        'SHELL=/this-shell-must-not-exist', 'app-default'], cwd=P/'gcc', text=True)
assert 'MODE=0' in result.splitlines(), result
original = O/'src/screens/menu'
count = 0
for f in original.rglob('*'):
    if f.is_file():
        assert f.read_bytes() == (P/'src/screens/menu'/f.relative_to(original)).read_bytes(), f
        count += 1
assert count > 0
screen = (P/'src/helpers/display/lvgl_display.c').read_text()
assert '#if WATCH_GPU_SMOKE_TEST\n    watch_gpu_smoke_screen();\n#else\n    screen_menu_create();' in screen
rtos = (P/'src/rtos.c').read_text()
assert '#if !WATCH_GPU_SMOKE_TEST' in rtos
assert 'xTaskCreate(lightSens_task' in rtos and 'xTaskCreate(acclSens_task' in rtos
utils = (P/'src/helpers/utils/utils.c').read_text()
assert '#if !WATCH_GPU_SMOKE_TEST' in utils
print(f'PASS: plain make selects original application; {count} original menu files unchanged; sensor tasks enabled in app mode')
