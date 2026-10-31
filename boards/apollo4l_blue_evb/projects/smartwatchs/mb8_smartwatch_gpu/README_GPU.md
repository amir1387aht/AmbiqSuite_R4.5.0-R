# Original Mi Band 8 application with Apollo4 Lite Nema GPU

This sibling project runs the original `mb8_smartwatch` menu with SDK-matched
Ambiq LVGL 8.1.0 and Apollo4 Lite Nema GPU support. The original project is
untouched. The GPU project keeps the original menu, assets, touch handling,
fonts and bottom-middle FPS/CPU overlay. There is no alternate test UI.

## Current configuration

- `screen_menu_create()` always runs; the commented `ui_init()` stays commented.
- `LV_USE_PERF_MONITOR=1` at `LV_ALIGN_BOTTOM_MID`, enforced at compile time.
- Original Montserrat font options (8..48, even sizes), logging off, memory
  monitor off. Original 16 ms display/input periods are unchanged.
- Light sensor initialization/task remain enabled.
- Accelerometer initialization remains enabled, including its single status
  line. At the user's request, `acclSens_task()` deletes itself and returns
  immediately, so it does not read/format/print the continuous sensor stream.
  A bare task return is unsafe in FreeRTOS; `vTaskDelete(NULL)` comes first.
- GPU power is retained. Production deep-sleep/power cycling is not qualified.

## Build and flash

Use Arm GNU Toolchain 14.2.Rel1 (GCC 14.2.1), GNU make and a POSIX shell
(e.g. Git Bash/MSYS2). From this project's `gcc` directory:

```sh
make clean
./flash.bat
```

For on-device profiling, use the explicit wrapper:

```sh
./flash_profile.bat
```

It sets `GPU_PROFILE=1` for the existing flasher's nested make. Do not use
`make GPU_PROFILE=1 && ./flash.bat`: the second make normally rebuilds with
profiling off. Alternatively, `GPU_PROFILE=1 ./flash.bat` in Git Bash works.
Plain `./flash.bat` returns to profiling-off unless the environment exports it.

The original make/J-Link invocation, build-failure gate, device `AMA4B2KP-KXR`,
SWD/4000 kHz and application origin `0x00018000` are retained. The existing path
is `C:\Users\Padidar\SEGGER\JLink_V812a\JLink.exe`. These are inherited
settings, not independent silicon identification. No physical flashing was
performed by the agent. Keep known-good firmware and working SWD recovery.

Output: `gcc/bin/mb8_smartwatch_gpu.bin`, `.axf` and `.map`.
Source discovery uses GNU make wildcards rather than shell find/sort.
Changing GPU_PROFILE or GPU_CONVERT updates the compiler-flag stamp and rebuilds
objects, even when reusing an output directory.

Separate builds without flashing:

```sh
make -j8
make CONFIG=bin-profile GPU_PROFILE=1 -j8
make CONFIG=bin-cpu GPU_PROFILE=1 GPU_CONVERT=0 -j8
```

`GPU_CONVERT=0` uses CPU format conversion/copying but keeps the new two-frame
pipeline and GPU UI renderer. It is an A/B control, not a restoration of an older
commit. To flash that control from Git Bash:

```sh
GPU_CONVERT=0 ./flash_profile.bat
```

## Why the transport pipeline changed

The user's active-scrolling logs showed about 29 completed transfers/second
while TE stayed near 59 Hz. Reported conversion time was 359 ms per profiling
interval, roughly 12 ms per completed frame. The old pipeline converted BGRA
pixels on the CPU and shared one panel staging frame, preventing writes to that
frame while TE/DMA owned it. Those are concrete serialization costs; the TE
signal does not establish a fundamental 30 Hz panel limit.

The current pipeline:

1. Retain the vendor's required 32-bit BGRA8888 LVGL ABI and the two small
   192x64 render buffers. Do not mix the original LVGL 8.4 sources with the archive.
2. Keep two separate 192x490 RGB565 panel frames. One stays in the existing static
   general shared region; the second is allocated, 32-byte aligned, from the
   existing GPU shared heap. Stop with an explicit message if allocation fails.
3. At the first partial flush of a refresh, select the frame other than the
   latest submitted frame. Copy the latest complete RGB565 image into it using
   Nema, preserving all unchanged pixels for arbitrary partial invalidations.
   GPU and MSPI may read the latest frame concurrently; neither writes it.
4. Convert each completed BGRA rectangle with a Nema source-copy blit to RGB565,
   using explicit row strides and clipping. Round the destination binding down
   to a 64-bit boundary and clip past its prefix so narrow/even-column updates
   do not violate texture alignment or overwrite neighboring pixels.
5. Complete each conversion before LVGL reuses its source draw buffer. Union
   dirty rows across partial rectangles and send one full-width dirty-row range
   only on the last flush. Unchanged rows are not transmitted.
6. Before queuing that new panel frame, wait for the previous panel transfer to
   finish using the ISR-signaled semaphore. Keep the original TE-gated driver;
   do not remove TE or raise its MSPI clock to make a benchmark look faster.
7. Once queued, release the LVGL draw buffer immediately: MSPI owns the separate
   RGB565 frame, not that BGRA draw buffer. The next refresh can render into the
   other panel frame while the preceding frame waits for TE or transfers.
8. The DMA ISR increments the completed-transfer count and signals the semaphore;
   it no longer releases LVGL flushes. Sleep waits for panel idle before shutdown.

The conversion CL is allocated once (1024 bytes) and rewound only after completion.
Unsupported/allocation/command-build failures fall back to the CPU reference
conversion. A submitted transfer/wait failure stops rather than handing incomplete
pixels to MSPI. CPU-written shared pixels are explicitly committed before GPU
reads/DAXI invalidation. Shared SDK files are not modified; no external PSRAM,
EVB shield display stack, DSI or NemaDC transport is imported.

## GPU text and CPU fallback

Immutable packed MRAM A4 fonts are converted into a 96 KiB shared-SRAM glyph
cache for direct Nema text rendering. Original bitmaps/descriptors stay packed;
`LV_AMBIQ_FONT_REALIGN_BITMAP` stays zero for correct CPU fallback interpretation.
The cache has 256 entries and does not evict GPU-referenced data. Full/unsupported
or dynamic glyphs submit/finish the open text batch before CPU fallback. Tiny
masked fills of at most 256 pixels use CPU blending to avoid per-scanline GPU
submit/wait overhead. This threshold is a tuning choice, not a measured optimum.
Task pacing follows LVGL's next deadline rather than always sleeping another 5 ms.

## Runtime checks and profiling

A GPU-conversion build should print:

```text
GPU conversion: Nema BGRA8888 -> RGB565 enabled.
GPU glyph-cache-v1: FPS monitor=1 refresh=16 ms profile=1
Panel pipeline: double RGB565 frames, render/DMA overlap.
GPU TEXT completed: submitted=... HW_CLID=... glyphs=...
```

The text completion check now captures the submitted ID BEFORE `nema_cl_wait()`:
the SDK clears that field to -1 after a successful wait, which previously
suppressed the one-time confirmation. The log checks hardware CLID advancement;
it does not prove all primitives, correct colors or a particular frame rate.

With profiling enabled, keep the watch awake and scroll the same menu continuously
for several seconds. Capture complete `GPUprof` lines:

- `dma_fps`: actual completed panel DMA transfers/second, not LCD scan frequency.
  The LVGL overlay can count a queued frame before DMA finishes; use dma_fps to
  compare delivered frames, not just rendered/queued frames.
- `te_hz`: observed TE edges/second.
- `cl`, `glyph_gpu`, `glyph_cpu`, `cache_new`, `cache_hit`: interval counts.
- `conv_gpu`, `conv_cpu`: rectangle conversions by path (frame snapshots excluded).
- `handler_ms`: inclusive LVGL handler interval total.
- `convert_ms`: rectangle conversion setup/wait time; now GPU rather than the old
  CPU loop when conv_gpu is positive. `framecopy_ms` separately tracks snapshots.
- `panelwait_ms`: time blocked waiting to reuse the preceding transfer's slot;
  no longer includes waiting for the newly queued transfer inside LVGL.
- `tewait_ms`, `dma_ms`: queued-to-TE and TE-launch-to-callback interval totals.
- `gpuwait_ms`: LVGL drawing-command waits (conversion waits are in convert_ms).
- `hw`: latest observed completed hardware CLID.

DWT-cycle timings assume configured 192 MHz and can be affected by clock gating,
sleep and other tasks. Totals overlap/nest and must not be added into utilization
percentages. UART profiling adds overhead and is off by default. The ordinary
FPS/CPU overlay is unchanged; its CPU figure is LVGL handler busy/idle timing,
not an independent FreeRTOS CPU-utilization measurement.

60 FPS remains the target, not an agent-measured result. Compile/link and host
command tests cannot verify the watch's rasterizer, panel timing, colors or speed.

## Memory layout

| Region | Start | Reserved/available size |
| --- | --- | --- |
| Application MRAM | 0x00018000 | 1,998,848 bytes |
| CPU TCM | 0x10000000 | 256 KiB |
| LVGL host TCM pool | 0x10040000 | 128 KiB reserved |
| General shared SRAM | 0x10060000 | 512 KiB |
| LVGL GPU shared pool | 0x100e0000 | 512 KiB reserved |

The static general shared region includes the MSP/C/FreeRTOS heaps, BGRA buffers
and first 188,160-byte panel frame. Current default linked usage is 428,288 bytes
(81.69%). In the separately reserved GPU shared pool, the glyph arena consumes
96 KiB, the second panel allocation requests 188,191 bytes including alignment
slack, and the conversion CL requests 1024 bytes, in addition to SDK/allocator
storage. Runtime checks bound the second panel frame inside that GPU pool.
Do not count it as another static frame in the general shared region.

Vendor archives (already in the repository):
- `third_party/LVGL/ambiq_support/gpu_lib_apollo4/gcc/bin/lvgl_ambiq_porting.a`
- `third_party/ThinkSi/config/apollo4l_nemagfx/gcc/bin/lib_nema_apollo4l_nemagfx.a`

## Regression checks

From this project's root, with Arm tools on PATH:

```sh
python3 tests/test_app_default.py
python3 tests/test_source_discovery.py
python3 tests/test_flash_config.py
python3 tests/test_display_pipeline.py
python3 tests/test_gpu_glyph_path.py
python3 tests/test_font_cache_assets.py
python3 tests/test_gpu_conversion.py
python3 tests/test_gpu_convert_host.py
python3 tests/validate_port.py
cc -std=c11 -O2 -Wall -Wextra -Werror -Ilibs/gpu_port tests/test_rgb565.c -o /tmp/test_rgb565
/tmp/test_rgb565
cc -std=c11 -O2 -Wall -Wextra -Werror -Ilibs/gpu_port tests/test_framebuffer.c -o /tmp/test_framebuffer
/tmp/test_framebuffer
cc -std=c11 -O2 -Wall -Wextra -Werror -Ilibs/gpu_port tests/test_glyph_cache.c -o /tmp/test_glyph_cache
/tmp/test_glyph_cache
python3 tests/verify_firmware.py
python3 tests/verify_firmware.py bin-profile
python3 tests/verify_firmware.py bin-cpu
```

The conversion host test compiles production conversion code with actual SDK API
headers but mocked Nema calls. It validates commands/strides/alignment, ordered
completion, partial-pixel preservation using CPU reference execution and fallback
behavior. It is not a GPU emulator or hardware color-validation result.
