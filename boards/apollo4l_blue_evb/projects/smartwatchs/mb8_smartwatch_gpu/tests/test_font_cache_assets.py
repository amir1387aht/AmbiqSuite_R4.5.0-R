#!/usr/bin/env python3
"""Run the actual row converter against all glyphs in the five local SDK fonts."""
from pathlib import Path
import ctypes
import os
import re
import subprocess
import tempfile
P = Path(__file__).resolve().parents[1]
glyphs = pixels = 0
with tempfile.TemporaryDirectory() as d:
    d = Path(d)
    (d/'align.c').write_text('#include "watch_glyph_cache_core.h"\nvoid align_a4(unsigned char *d, const unsigned char *s, unsigned w, unsigned h) { watch_a4_align_rows(d,s,w,h); }\n')
    subprocess.run([os.environ.get('CC', 'cc'), '-std=c11', '-O2', '-shared', '-fPIC',
        '-I'+str(P/'libs/gpu_port'), str(d/'align.c'), '-o', str(d/'align.so')], check=True)
    lib = ctypes.CDLL(str(d/'align.so'))
    lib.align_a4.argtypes = [ctypes.c_void_p, ctypes.c_void_p, ctypes.c_uint, ctypes.c_uint]
    for size in [14, 20, 28, 44, 48]:
        text = (P/f'libs/gpu_port/lv_font_montserrat_{size}.c').read_text()
        bitmap = re.search(r'glyph_bitmap\[\] = \{(.*?)\n\};', text, re.S)[1]
        data = bytes(int(x, 16) for x in re.findall(r'0x([0-9a-fA-F]+)', bitmap))
        descriptors = re.findall(r'\{\.bitmap_index = (\d+), \.adv_w = \d+, \.box_w = (\d+), \.box_h = (\d+)', text)
        assert descriptors
        for offset, width, height in descriptors:
            offset, width, height = map(int, [offset, width, height])
            if not width or not height: continue
            n = (width * height + 1) // 2
            src = data[offset:offset+n]
            assert len(src) == n
            source = (ctypes.c_uint8 * n).from_buffer_copy(src)
            pitch = (width + 1) // 2
            result = (ctypes.c_uint8 * (pitch * height))()
            lib.align_a4(result, source, width, height)
            for y in range(height):
                for x in range(width):
                    i = y * width + x
                    expected = (src[i//2] >> (0 if i & 1 else 4)) & 15
                    actual = (result[y*pitch + x//2] >> (0 if x & 1 else 4)) & 15
                    assert actual == expected, (size, offset, x, y)
                    pixels += 1
                if width & 1: assert result[y*pitch + pitch - 1] & 15 == 0
            glyphs += 1
print(f'PASS: actual cache converter preserves {pixels} pixels across {glyphs} glyphs in the five local SDK fonts')
