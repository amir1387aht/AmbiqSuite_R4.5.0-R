# Mi Band 8 GPU pipeline: build and validation report

Toolchain: Arm GNU Toolchain 14.2.Rel1 / GCC 14.2.1, Cortex-M4,
FPv4-SP-D16, hard-float ABI. Real vendor libraries linked, no stubs in firmware.

## Current behavior

Original menu, touch, assets, fonts and bottom-middle LVGL FPS/CPU monitor are
preserved. The GPU sibling now uses Nema BGRA8888-to-RGB565 conversion and two
panel staging frames, overlapping next-frame rendering with the preceding
TE/DMA transfer. Partial refreshes snapshot the latest submitted frame before
modifying the alternate frame; the DMA-owned frame remains read-only. TE and
48 MHz MSPI configuration are unchanged.

At the user's request, the accelerometer task calls vTaskDelete(NULL) then returns
immediately. Initialization remains enabled. There are no continuous sensor
reads or stream prints from that task. No alternate UI or mode was introduced.
All 1,258 original-project tracked files remain unchanged.

The one-time completion log now saves CL submission_id before waiting: vendor
archive disassembly confirmed that nema_cl_wait resets it to -1 on success.

## Compile/link results

| Configuration | Units | Binary/MRAM bytes | Reset vector | GPU IRQ 28 vector |
| --- | --- | --- | --- | --- |
| bin | 202 | 396,744 | 0x467f5 | 0x43ee1 |
| bin-profile | 202 | 397,680 | 0x46ad5 | 0x441c9 |
| bin-cpu | 202 | 396,884 | 0x46905 | 0x43ff9 |

SHA-256:
- `bin`: `cd4c5fb31160826824fe245eed2890141a5d449e1427e7627f9cea293bce61a4`
- `bin-profile`: `ccf4efe3550bcc34c25ad846e6ac2a4aa829be0e2caaab9a532b3b4d25585293`
- `bin-cpu`: `6b78c7092d9f05de80b9c0c4007fc31fdf6d30c531004baecef78a88fc22b628`

All initial stack pointers: 0x10061000. Each ELF has zero unresolved symbols;
GPU IRQ 28 resolves to the strong SDK ISR, not the default handler. Existing
SDK/watch compiler warnings remain; this is not a warnings-free claim.

Default linked CPU TCM use: 4,888 bytes. General shared SRAM:
- Default: 428,288 bytes (81.69% of 512 KiB).
- Profile: 428,384 bytes (81.71%).
- CPU-conversion profile control: 428,352 bytes (81.70%).

Separately reserved: 128 KiB LVGL TCM and 512 KiB GPU shared heap. Runtime GPU-heap
requests include 96 KiB glyph arena, 188,191 bytes for the second panel frame plus
alignment slack, and a 1024-byte conversion CL, besides SDK/allocator storage.
Allocation failures are checked; the panel frame is runtime-bounded to the GPU
heap region and 32-byte aligned. No external PSRAM is used.

## Passed checks

- All firmware variants compile/link with SDK LVGL 8.1 and the real Apollo4l Nema
  and Ambiq LVGL archives; no LVGL 8.4 source dependencies.
- Linked FPS/CPU monitor text, startup/pipeline markers, GPU conversion/copy
  functions, vectors, aligned static buffers and linker-pool boundaries verified.
- Requested accelerometer stream text is absent from linked firmware.
- GNU make discovers 26 application and 130 remaining SDK LVGL sources without
  invoking a shell during discovery, plus the local/vendor/RTOS sources.
- Original menu and non-port LVGL font/monitor/log settings preserved; sensor
  source comparison permits only the explicitly requested safe task stop.
- Two-frame integration ownership/order checked: snapshot latest to alternate,
  finish GPU writes before source reuse, wait before transfer-slot reuse, queue
  once on last flush, release LVGL in task context; ISR only signals DMA complete.
- Production conversion C compiled on host with SDK API headers and mocked Nema
  calls for both GPU_CONVERT values. Exercised eight full-frame stripes and 435
  partial rectangles, RGB565 full-frame snapshot, 64-bit binding prefixes,
  explicit source/destination strides, source-copy blending, wait-before-reuse,
  submission-ID reset side effect, allocation and command-build fallback.
- All 65,536 RGB565 reference colors round-trip through BGRA8888.
- All 12,577 migrated asset pixels preserve source color/alpha; all 18,528
  horizontal invalidation ranges fit panel/buffer bounds.
- 4,225 A4 layouts validate odd-width padding/guards/cache exhaustion; 780 actual
  glyphs / 457,432 pixels match their source bitmaps after row alignment.
- Fresh 202-unit default build in bin-final is byte-identical to bin; both ELF
  verifications pass. Profiling/conversion flag stamps update on toggles and
  stay stable for unchanged options.
- Existing flash.bat and profiling wrapper are unchanged in this follow-up.

## Scope and limits

The supplied pre-change profile showed about 29 completed DMA frames/s with
roughly 59 TE edges/s and about 12 ms of CPU format-conversion cost per completed
frame. These observations motivated GPU conversion and rendering/transfer overlap;
they do not establish new-build performance.

No physical watch, GPU simulator or J-Link probe was available. Host command tests
use CPU reference execution and do not verify hardware RGB565 rasterization,
byte order, actual TE/DMA timing, menu appearance, touch/sensors, power or sleep.
No 60 FPS result or speedup is claimed from compilation. Retest the same active
menu scroll with ./flash_profile.bat and compare dma_fps, conv_gpu/conv_cpu,
convert_ms, framecopy_ms, tewait_ms, dma_ms and panelwait_ms. Inclusive DWT totals
are not additive CPU-utilization percentages.
