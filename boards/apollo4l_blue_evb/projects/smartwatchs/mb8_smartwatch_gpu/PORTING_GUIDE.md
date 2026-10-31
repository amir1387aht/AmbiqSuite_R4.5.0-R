# How the Apollo4 Blue Lite / Mi Band 8 GPU port works

## What was missing, and what already existed

This was not a GPU driver written from scratch. AmbiqSuite R4.5.0 already contains
three important pieces even though there is no ready-to-build GPU watch example
for this board:

1. The **Apollo4 Lite chip-specific Nema platform HAL** in
   `third_party/ThinkSi/config/apollo4l_nemagfx/` and its
   `gcc/bin/lib_nema_apollo4l_nemagfx.a` archive.
2. The **SDK-matched LVGL 8.1.0 + Ambiq Nema integration** under
   `third_party/LVGL/lvgl/` and `third_party/LVGL/ambiq_support/`, including
   `gpu_lib_apollo4/gcc/bin/lvgl_ambiq_porting.a`.
3. The existing watch application, Raydium/MSPI panel driver, touch driver and
   Apollo4l MCU/BSP/HAL under `mb8_smartwatch` and this board.

The work was to connect those pieces with the right ABI, memory ownership,
startup, interrupts and panel transport. Apollo4b graphics examples helped with
SDK integration patterns, not with copying their board/pins/PSRAM/display stack.
The upstream Ambiq LVGL repositories are useful references, but their current
branches are not interchangeable with this SDK's precompiled archive.

## 1. Keep the original project; create a GPU sibling

All work is in `mb8_smartwatch_gpu`. The original `mb8_smartwatch` remains intact.
The sibling keeps the application's menu, widgets, fonts, assets and touch/panel
protocol. The accelerometer task now deletes itself and returns at the user's
request during performance testing; initialization still runs.

The Makefile defines PART_apollo4l / AM_PART_APOLLO4L, Cortex-M4, FPv4-SP-D16,
hard-float, the Apollo4l Nema platform and the Ambiq custom allocator. It builds
real vendor libraries, not placeholder implementations. Source discovery uses
GNU make wildcards so Windows find.exe cannot silently drop main/RTOS sources.

## 2. Match LVGL and the vendor archive ABI first

The original application used LVGL 8.4; this vendor SDK contains LVGL 8.1.0.
The initial working GPU baseline uses the SDK version, not a mixture of versions.
Public/internal structures and draw function signatures must match the compiled
Ambiq archive. Upgrading LVGL is a separate porting task after this baseline.

The chosen archive expects **32-bit LVGL color**, so render buffers use
BGRA8888, even though the watch panel takes RGB565. Changing LV_COLOR_DEPTH to 16
without rebuilding/replacing the archive would break that ABI. Generated assets
were adapted to the 32-bit descriptors while retaining source colors and alpha;
small local font wrappers reconcile SDK declarations without changing bitmaps.

Relevant files: `src/lv_conf.h`, `gcc/Makefile`, local font overrides and
`libs/gpu_port/lv_gpu_ambiq_nema.c`.

## 3. Put pixels and GPU commands where the GPU can access them

CPU TCM is not a general substitute for GPU-visible shared SRAM. This port uses
linker-bounded regions instead of importing example PSRAM addresses:

- Application MRAM: starts at 0x00018000 (existing watch image origin).
- CPU TCM: 0x10000000, 256 KiB.
- LVGL host TCM pool: 0x10040000, 128 KiB.
- General shared SRAM: 0x10060000, 512 KiB.
- LVGL GPU shared pool: 0x100e0000, 512 KiB; ends at 0x10160000.

Nema command lists, GPU source textures/glyphs and destination buffers use shared
memory. Two 192x64 BGRA render buffers and one RGB565 panel frame are statically
aligned in general shared SRAM. The second panel frame, A4 glyph arena and
conversion command list are allocated from the existing GPU shared pool.

Texture base addresses must be 64-bit aligned. Panel buffers are 32-byte aligned;
narrow RGB565 rectangles bind at an earlier 64-bit address and clip past the
prefix. Explicit strides preserve the untouched columns between rectangle rows.
CPU-produced shared pixels are flushed before GPU/DMA reads; the SDK GPU ISR
invalidates DAXI for CPU visibility after GPU completion.

Relevant files: `gcc/linker_script.ld`, local `lv_mem.c` / `lv_tlsf.c`,
`watch_gpu_convert.c` and `watch_framebuffer.h`.

## 4. Bring up the actual Apollo4l GPU, not an EVB display controller

After SRAM power configuration and before other DMA clients, `watch_gpu_init()`
calls LVGL/Nema initialization with the Apollo4l register definitions and HAL.
The GPU USER power request is retained while active. External PSRAM, NemaDC,
DSI and EVB shield display wiring are not initialized for this watch.

Startup vector IRQ 28 must reach the real `am_gpu_isr`, not a weak default ISR.
The Nema completion semaphore must exist and its interrupt priority must permit
FreeRTOS FromISR APIs. The local `watch_nema_hal.c` now additionally fixes the SDK
binary-semaphore branch: initialize xHigherPriorityTaskWoken, publish completion,
and call portYIELD_FROM_ISR for both RTOS wake mechanisms. Otherwise a completed
conversion can wait for another scheduler tick before the LVGL task resumes. Rebase these local ISR fixes
when upgrading the SDK rather than retaining a stale HAL copy.

All LVGL and command-list ownership stays on the LVGL task. GPU and panel-DMA
completion signals are separate. Sleep/wake drains outstanding work instead of
suspending the graphics task in the middle of GPU/DMA operations. Deep-sleep and
production power cycling still need independent qualification.

## 5. Make the real panel transport efficient

The watch's existing panel interface remains RGB565 over TE-gated MSPI. The first
GPU attempt was slow not because the hardware lacked GPU support, but because
each 64-line LVGL stripe was sent as a separate TE-gated panel update. A full
490-line refresh then consumed several TE opportunities. CPU conversion was
another major cost even after those stripes were combined.

The current pipeline is:

```text
LVGL 8.1 + Nema render -> small BGRA shared buffers
    -> Nema BGRA-to-RGB565 blit into alternate full panel frame
    -> one dirty-row-range transfer queued for TE
    -> original MSPI/Raydium DMA -> panel
```

At the beginning of a partial refresh, copy the latest submitted RGB565 image
into the alternate frame, then overwrite only the invalid rectangles. This keeps
unchanged pixels correct without modifying the frame MSPI owns. GPU conversion
finishes before its BGRA source is reused. Once the separate RGB565 frame is
queued, LVGL can render the next refresh while the preceding transfer waits for
TE or DMA. Wait only before reusing the panel transfer slot.

Relevant files: `src/helpers/display/lvgl_display.c`, `display.c`,
`libs/gpu_port/watch_gpu_convert.c`.

## 6. Make packed fonts safe for direct GPU text

The original A4 font bitmap is densely packed across rows and lives in MRAM.
The GPU path needs accessible source memory and byte-aligned A4 rows; odd-width
glyphs are especially easy to get wrong. A 96 KiB shared-SRAM cache converts
immutable MRAM glyphs on first use, keeps at most 256 entries and does not evict
pointers that GPU commands may still reference.

The global LV_AMBIQ_FONT_REALIGN_BITMAP flag stays zero: CPU fallback still reads
the original packed bitmaps. Unsupported/dynamic glyphs or cache exhaustion first
submit/finish any open text batch, then use CPU rendering safely. Tiny masked
fills stay on CPU to avoid many tiny GPU submit/wait cycles. This is a hybrid
renderer, not a claim that every LVGL operation runs on the GPU.

Relevant files: `watch_glyph_cache*.c/.h`, `lv_gpu_ambiq_nema.c`.

## 7. Verify execution and pacing, not just an enabled flag

Compilation proves ABI/link consistency, not hardware rendering. The build checks
vectors, memory boundaries, actual FPS/CPU overlay text and unresolved symbols.
Host tests cover font bits, rectangle/color conversion, command contracts,
stride/alignment, buffer ordering and the actual ISR/profiler control paths with
mocks. They are not a GPU simulator.

On the watch, GPU TEXT completed compares a saved submitted ID with the hardware
CLID after waiting. Saving it before waiting matters: the vendor wait resets the
field to -1 on success. GPUprof shows accepted GPU glyphs, conversion path counts,
actual DMA completions and TE cadence. gap1/gap2/gap3p distinguish consecutive
panel opportunities from repeated/late frames while actively scrolling.

Profiling previously printed synchronously on the graphics task. It now hands
snapshots to a lower-priority logger, so slow UART transmission is not an inline
frame-rendering step. For the cleanest feel comparison, use
`./flash.bat GPU_PROFILE=0`; the FPS/CPU overlay remains on. Use the same flasher
with GPU_PROFILE=1 for diagnostics. No separate profiling flasher is required.

## Common traps this integration avoids

- Correct GPU flag but wrong LVGL archive version or color layout.
- Correct CPU pointers that the GPU cannot access, or example PSRAM assumptions.
- Packed/odd-width font data passed directly to an aligned A4 GPU texture.
- Unaligned texture bases or forgetting RGB565 destination row stride.
- Weak/default GPU ISR, unsafe RTOS IRQ priority, or semaphore wake without yield.
- One TE wait per small render stripe instead of per completed logical frame.
- Releasing a source before GPU completion, or writing a frame while MSPI reads it.
- Judging smoothness from average FPS alone while logging blocks the UI task.

Stock firmware can also use different touch processing and scroll physics.
Scheduling fixes do not establish identical behavior to Xiaomi's implementation;
remaining differences need on-device comparison after frame cadence is consistent.
