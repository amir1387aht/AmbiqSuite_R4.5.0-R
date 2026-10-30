#!/usr/bin/env python3
"""Static flash-script checks; does not execute Windows or connect to hardware."""
from pathlib import Path
import re
P = Path(__file__).resolve().parents[1]
batch = (P / 'gcc/flash.bat').read_text()
commands = (P / 'gcc/jlink_auto_file.jlink').read_text()
linker = (P / 'gcc/linker_script.ld').read_text()
assert 'set "GPU_MODE=1"' in batch
assert 'call make all GPU_SMOKE_TEST=%GPU_MODE% CONFIG=bin' in batch
build = batch.index('call make all')
check = batch.index('if errorlevel 1 goto build_failed', build)
flash = batch.index('"%JLINK_EXE%" -device')
assert build < check < flash
assert '-ExitOnError 1' in batch and 'exit /b %FLASH_RC%' in batch
assert 'AMA4B2KP-KXR' in batch and '-if SWD -speed 4000' in batch
assert commands.splitlines() == ['connect', 'r', 'h',
    'loadfile "./bin/mb8_smartwatch_gpu.bin", 0x00018000', 'r', 'g', 'exit']
origin = re.search(r'MCU_MRAM[^\n]*ORIGIN\s*=\s*(0x[0-9a-fA-F]+)', linker)
assert origin and int(origin[1], 16) == 0x18000
assert 'mb8_smartwatch.bin' not in batch + commands
print('PASS: GPU image only; default smoke; build-failure gate; original SWD/origin; J-Link error propagation')
print('Static checks only: Windows execution and hardware flashing not tested')
