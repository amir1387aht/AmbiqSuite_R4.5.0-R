#!/usr/bin/env python3
"""Check GNU make discovers every source without launching any shell/find/sort."""
from pathlib import Path
import os
import subprocess
import tempfile

P = Path(__file__).resolve().parents[1]
G = P / 'gcc'
R = P.parents[4]
makefile = (G / 'Makefile').read_text()
assert '$(shell' not in makefile, 'Source discovery must not depend on a shell'
with tempfile.TemporaryDirectory() as temp:
    manifest = Path(temp) / 'manifest.mk'
    manifest.write_text('.PHONY: source-manifest\nsource-manifest:\n'
                        '\t$(info APP=$(APP_SRC))\n'
                        '\t$(info LVGL=$(LVGL_SRC))\n'
                        '\t$(info DIRS=$(APP_DIRS))\n')
    result = subprocess.check_output(
        [os.environ.get('MAKE', 'make'), '--no-print-directory', '-s',
         '-f', 'Makefile', '-f', str(manifest),
         'SHELL=/this-shell-must-not-exist', 'source-manifest'], cwd=G, text=True)
rows = dict(line.split('=', 1) for line in result.splitlines() if '=' in line)
app = set(rows['APP'].split())
expected = {'../' + f.relative_to(P).as_posix() for f in (P / 'src').rglob('*.c')}
assert app == expected, (expected - app, app - expected)
assert '../src/main.c' in app and '../src/rtos.c' in app
sdk = R / 'third_party/LVGL/lvgl/src'
skip = {'misc/lv_mem.c', 'misc/lv_tlsf.c', 'gpu/lv_gpu_ambiq_nema.c'}
skip |= {f'font/lv_font_montserrat_{size}.c' for size in [14, 20, 28, 44, 48]}
expected_lvgl = {'../../../../../../third_party/LVGL/lvgl/src/' + f.relative_to(sdk).as_posix()
                 for f in sdk.rglob('*.c') if f.relative_to(sdk).as_posix() not in skip}
assert set(rows['LVGL'].split()) == expected_lvgl
expected_dirs = {'../src'} | {'../' + f.relative_to(P).as_posix()
                             for f in (P / 'src').rglob('*') if f.is_dir()}
assert set(rows['DIRS'].split()) == expected_dirs
print(f'PASS: {len(app)} application sources, {len(expected_lvgl)} SDK LVGL sources, '
      f'{len(expected_dirs)} application include directories; no shell executable available')
