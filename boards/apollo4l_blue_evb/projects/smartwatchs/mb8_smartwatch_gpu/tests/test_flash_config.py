#!/usr/bin/env python3
"""Static checks for the user's original simple Windows/J-Link workflow."""
from pathlib import Path
import re
P = Path(__file__).resolve().parents[1]
batch = (P/'gcc/flash.bat').read_text()
commands = (P/'gcc/jlink_auto_file.jlink').read_text()
original = (P.with_name('mb8_smartwatch')/'gcc/flash.bat').read_text().splitlines()
assert batch.splitlines()[0] == 'make %*'
assert not (P/'gcc/flash_profile.bat').exists()
assert batch.splitlines()[1] == 'if errorlevel 1 exit /b 1'
assert batch.splitlines()[2] == original[1]
assert 'loadfile "./bin/mb8_smartwatch_gpu.bin", 0x00018000' in commands
origin = re.search(r'MCU_MRAM[^\n]*ORIGIN\s*=\s*(0x[0-9a-fA-F]+)', (P/'gcc/linker_script.ld').read_text())
assert origin and int(origin[1], 16) == 0x18000
print('PASS: make arguments forwarded; original J-Link invocation, build-failure gate, GPU image at original origin')
print('Static checks only; Windows execution and hardware flashing not tested here')
