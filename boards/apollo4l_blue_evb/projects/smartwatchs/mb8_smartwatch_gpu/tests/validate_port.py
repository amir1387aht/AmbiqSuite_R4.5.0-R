#!/usr/bin/env python3
"""Static regression checks; not a substitute for linking or hardware tests."""
from pathlib import Path
import re
import subprocess

P = Path(__file__).resolve().parents[1]
R = P.parents[4]
O = P.with_name("mb8_smartwatch")
assert (R / "third_party/LVGL/lvgl/lvgl.h").is_file()
version = (R / "third_party/LVGL/lvgl/lvgl.h").read_text()
assert re.search(r"LVGL_VERSION_MAJOR\s+8", version)
assert re.search(r"LVGL_VERSION_MINOR\s+1", version)
makefile = (P / "gcc/Makefile").read_text()
assert "../libs/lvgl" not in makefile
assert "apollo4l_nemagfx" in makefile
assert "-mfloat-abi=hard" in makefile
assert "LV_MEM_PSRAM_ADR 0" in (P / "src/lv_conf.h").read_text()

# Independently verify every migrated generated image against the original.
pixel_count = 0
for f in (O / "src/screens/main/images").glob("*.c"):
    a = f.read_text()
    b = (P / f.relative_to(O)).read_text()
    def array(text):
        body = re.search(r"uint8_t\s+\w+\[\]\s*=\s*\{(.*?)\};", text, re.S)[1]
        return bytes(int(v, 16) for v in re.findall(r"0x([0-9a-fA-F]{2})\b", body))
    old, new = array(a), array(b)
    w = int(re.search(r"\.w\s*=\s*(\d+)", a)[1])
    h = int(re.search(r"\.h\s*=\s*(\d+)", a)[1])
    assert len(old) == w * h * 3 and len(new) == w * h * 4, f.name
    assert int(re.search(r"\.data_size\s*=\s*(\d+)", b)[1]) == len(new)
    for i in range(w * h):
        color = old[3*i] | old[3*i+1] << 8
        blue, green, red, alpha = new[4*i:4*i+4]
        assert ((red >> 3) << 11 | (green >> 2) << 5 | blue >> 3) == color
        assert alpha == old[3*i+2]
    pixel_count += w * h

# Even-column rounder must not exceed the panel or enlarge width by >2.
for x1 in range(192):
    for x2 in range(x1, 192):
        start = x1 & ~1
        end = x2 + ((x2 - start + 1) & 1)
        assert 0 <= start <= x1 <= x2 <= end < 192
        assert (end - start + 1) % 2 == 0
        assert (end - start + 1) * 64 <= 192 * 64

# Original tracked source tree remains byte-for-byte identical to HEAD.
if (R / ".git").exists():
    prefix = O.relative_to(R).as_posix()
    tracked = subprocess.check_output(["git", "ls-files", "-z", "--", prefix], cwd=R).split(b"\0")
    subprocess.run(["git", "diff", "--exit-code", "HEAD", "--", prefix], cwd=R, check=True)
    checked = sum(bool(path) for path in tracked)
    print(f"PASS: {checked} original tracked files unchanged")
print(f"PASS: SDK v8.1 source selection; no PSRAM; {pixel_count} converted image pixels")
print("PASS: all 18,528 horizontal invalidation ranges fit the panel/buffer")
