#!/usr/bin/env python3
"""Compile real conversion source with SDK API headers and mocked Nema calls."""
from pathlib import Path
import subprocess,tempfile
P=Path(__file__).resolve().parents[1];R=P.parents[4]
with tempfile.TemporaryDirectory() as temp:
 t=Path(temp)
 (t/'FreeRTOS.h').write_text('#include <assert.h>\n#define configASSERT(x) assert(x)\n')
 (t/'am_util.h').write_text('int am_util_stdio_printf(const char *, ...);\n')
 # Only MCU status declarations are stubbed; production Nema headers are used.
 (t/'am_hal_status.h').write_text('typedef enum { AM_HAL_STATUS_SUCCESS=0 } am_hal_status_e;\n')
 includes=[t,P/'libs/gpu_port',R/'third_party/ThinkSi/config/apollo4l_nemagfx',R/'third_party/ThinkSi/NemaGFX_SDK/include/tsi/NemaGFX',R/'third_party/ThinkSi/NemaGFX_SDK/include/tsi/common',R/'third_party/ThinkSi/NemaGFX_SDK/NemaGFX']
 for mode in [0,1]:
  exe=t/f'test-{mode}'
  subprocess.run(['cc','-std=c11','-O2','-Wall','-Wextra','-Werror',f'-DWATCH_GPU_CONVERT={mode}','-DWATCH_GPU_PROFILE=0',*[f'-I{x}' for x in includes],str(P/'libs/gpu_port/watch_gpu_convert.c'),str(P/'tests/test_gpu_convert_commands.c'),'-o',str(exe)],check=True)
  subprocess.run([str(exe)],check=True)
