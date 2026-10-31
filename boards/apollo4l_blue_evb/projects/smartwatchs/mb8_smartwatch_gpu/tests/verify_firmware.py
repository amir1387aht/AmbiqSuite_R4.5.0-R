from pathlib import Path
import subprocess, struct, re, json, hashlib, os, sys
P=Path(__file__).resolve().parents[1]; R=P.parents[4]
import shutil
nm_path=shutil.which('arm-none-eabi-nm')
assert nm_path, 'Put arm-none-eabi-nm on PATH'
T=Path(nm_path).parent
results={}
for mode in [sys.argv[1] if len(sys.argv) > 1 else 'bin']:
 d=P/'gcc'/mode; elf=d/'mb8_smartwatch_gpu.axf'; binary=d/'mb8_smartwatch_gpu.bin'
 assert elf.exists() and binary.exists(),mode
 undefined=subprocess.check_output([str(T/'arm-none-eabi-nm'),'-u',str(elf)],text=True)
 assert not undefined.strip(),undefined
 nm=subprocess.check_output([str(T/'arm-none-eabi-nm'),'-n',str(elf)],text=True)
 symbols={m[2]:int(m[1],16) for line in nm.splitlines() if (m:=re.match(r'([0-9a-fA-F]+)\s+\w\s+(\S+)',line))}
 data=binary.read_bytes(); sp,reset=struct.unpack_from('<II',data)
 assert b'%u FPS\n%u%% CPU' in data, 'Original LVGL FPS/CPU monitor missing from linked image'
 assert b'GPU glyph-cache-v1: FPS monitor=' in data
 assert b'GPU TEXT completed: submitted=' in data
 assert 'watch_gpu_glyph_bitmap' in symbols
 assert 'watch_gpu_note_completed' in symbols
 assert 'glyph_cache' in symbols
 assert 'watch_gpu_convert_rect' in symbols and 'watch_gpu_copy_frame' in symbols
 assert b'Panel pipeline: double RGB565 frames, render/DMA overlap.' in data
 assert b'Accel [g]:' not in data, 'Requested accelerometer stream was not eliminated'
 assert (b'GPU conversion: Nema BGRA8888 -> RGB565 enabled.' in data or
         b'GPU conversion disabled: CPU reference path.' in data)
 assert not any(name.startswith(('watch_gpu_' + 'smoke', 'watch_gpu_submissions', 'watch_gpu_completed_waits', 'watch_panel_flushes')) for name in symbols)
 assert 0x10060000<=sp<0x100e0000 and sp%8==0
 assert reset&1 and (reset&~1)==symbols['Reset_Handler']
 gpu_vector=struct.unpack_from('<I',data,(16+28)*4)[0]
 assert gpu_vector&1 and (gpu_vector&~1)==symbols['am_gpu_isr']
 assert symbols['am_gpu_isr']!=symbols['am_default_isr']
 assert symbols['__lvgl_tcm_start__']==0x10040000 and symbols['__lvgl_tcm_end__']==0x10060000
 assert symbols['__lvgl_ssram_start__']==0x100e0000 and symbols['__lvgl_ssram_end__']==0x10160000
 assert symbols['_ebss']<=symbols['__lvgl_ssram_start__']
 for name in ['display_buffer1','display_buffer2','panel_pixels']:
  assert 0x10060000<=symbols[name]<0x100e0000 and symbols[name]%32==0,(name,symbols[name])
 assert symbols['_edata']<=symbols['__lvgl_tcm_start__']
 sized=subprocess.check_output([str(T/'arm-none-eabi-nm'),'-S',str(elf)],text=True)
 match=re.search(r'^([0-9a-fA-F]+) ([0-9a-fA-F]+) \w panel_pixels$',sized,re.M)
 assert match and int(match[2],16)==192*490*2
 assert int(match[1],16)+int(match[2],16)<=symbols['__lvgl_ssram_start__']
 depfiles=list((d/'obj').rglob('*.d'))
 assert all('../libs/lvgl' not in f.read_text() for f in depfiles)
 results[mode]={'binary_bytes':len(data),'sha256':hashlib.sha256(data).hexdigest(),'stack_pointer':hex(sp),'reset_vector':hex(reset),'gpu_irq28_vector':hex(gpu_vector),'compiled_units':len(list((d/'obj').rglob('*.o'))),'unresolved_symbols':0}
 print('PASS:',mode,json.dumps(results[mode]))
print('PASS: linked application firmware validated (not a hardware test)')
