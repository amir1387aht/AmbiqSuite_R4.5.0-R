# Original application + GPU: build report

SDK baseline: `db7b6b12e57861d05b85966cdfa0dae3a28c02e7`.
Toolchain: Arm GNU Toolchain 14.2.Rel1 / GCC 14.2.1,
Cortex-M4, FPv4-SP-D16, hard-float ABI.

## Current configuration

One application only: original menu, light sensor and accelerometer, with the
Apollo4 Lite Nema GPU integration and SDK LVGL 8.1.0. Original monitor/font/log
settings are restored: FPS/CPU monitor enabled at bottom-middle, all original
Montserrat sizes enabled, LVGL logging disabled, memory monitor disabled.
No alternate UI, diagnostic timer or smoke-test mode is compiled. One-time GPU
completion logging is present; per-second UART profiling is opt-in.

## Clean build result

`make -j8` in `gcc` successfully compiled 201 translation units and linked the
actual vendor GPU libraries. No unresolved ELF symbols remain. Existing SDK/watch
compiler warnings remain; this is not a warnings-free claim.

| Item | Value |
| --- | --- |
| Binary | gcc/bin/mb8_smartwatch_gpu.bin |
| Binary/MRAM bytes | 396,684 |
| SHA-256 | d11a2eab6f7241cd1f223bf646a2e193d45ba0b644249f954192e5f8b7355243 |
| Initial SP | 0x10061000 |
| Reset vector | 0x46605 |
| GPU IRQ 28 vector | 0x43cfd |
| CPU TCM used | 4,888 bytes |
| General shared SRAM used | 428,224 bytes |
| Additional reserved LVGL TCM pool | 128 KiB |
| Additional reserved LVGL GPU shared pool | 512 KiB |

## Passed checks

- Default application always calls original screen_menu_create(); original menu,
  main entry point and accelerometer code remain unchanged.
- Light/accelerometer initialization/tasks are unconditional.
- All original font settings, FPS/CPU monitor position, and logging settings match
  the original lv_conf.h. Linked binary contains the actual LVGL FPS/CPU format text.
- Source enumeration works without a shell: 26 application sources, 130 remaining
  SDK LVGL sources, plus local port/font override sources, for 201 units total.
- No undefined symbols; GPU IRQ reaches the strong SDK ISR, not default handler.
- SP/reset vectors, reserved heaps and aligned shared-SRAM buffers verified.
- Binary contains no removed diagnostic counters or alternate-screen symbol.
- No copied LVGL 8.4 sources in dependencies; no external PSRAM dependency.
- All 65,536 RGB565 colors round-trip through actual transport conversion.
- All 12,577 migrated asset pixels preserve source color and alpha.
- All 18,528 horizontal invalidation ranges fit panel/buffer bounds.
- All 1,258 original tracked project files remain unchanged.
- Simple original make/J-Link invocation restored with build-failure gate; GPU
  binary uses the original 0x00018000 application origin.

Vendor archive SHA-256:
- lvgl_ambiq_porting.a: b5dff43858880e45315ca4a13e5afa168bcfd4266695d565324db998649825b6
- lib_nema_apollo4l_nemagfx.a: 9620164c81b1120f9eb70249e709fa488d6c42f89278c95c1abfc5375a1659e1

## Boundaries

No physical watch/probe or Windows runtime was available to the agent. Script
checks are static; compile/link and host tests do not establish hardware rendering,
menu behavior, touch/sensors, actual frame rate, power or sleep/wake correctness.
Production deep-sleep integration remains deferred. GPU A4 glyph acceleration
is implemented with a per-glyph cache; hardware pixel correctness is unverified.
GPU-specific color ABI, memory layout, initialization, DMA synchronization and tick
adaptations remain; this is not a byte-identical build of the non-GPU application.

## Display transport regression correction

Identified in code: 64-line partial draw buffers were each submitted as their own
TE-gated panel frame, and the display wait callback was absent. The correction
stages all partial rectangles in a 188,160-byte RGB565 frame, submits the union
of dirty rows only on LVGL's last flush, and blocks on a dedicated DMA-completion
semaphore rather than polling. The existing panel driver, menu, sensors, fonts,
monitor and flash.bat are unchanged by this follow-up.

Additional passed checks:
- Actual strided conversion helper assembles a 192x490 frame from eight stripes.
- 435 partial rectangles preserve every untouched pixel.
- Dirty-row union/reset covers multiple invalid areas and complete redraws.
- Integration structure submits only on the last flush and releases the staging
  frame only after DMA; ISR priority is compatible with FreeRTOS semaphore APIs.
- Linked staging buffer size is exactly 188,160 bytes, aligned in shared SRAM,
  and ends before the GPU allocator pool. General shared usage is 81.68%.
- A fresh 201-unit build in bin-final produces the same binary as the default bin
  build; both ELF checks pass with no unresolved symbols.

No on-device performance improvement is claimed yet. Retest the same original
menu scroll on the watch and check both FPS and visual/transfer correctness.
Standard LVGL CPU percentage is handler busy/idle timing, including blocked
handler time; the monitor is unchanged, not substituted with a more flattering one.

## GPU glyph/pacing follow-up

Direct Nema label/letter rendering is now enabled through a 96 KiB shared-SRAM
A4 cache, without changing the original packed MRAM font descriptors or setting
the global realigned-font flag. Maximum 256 entries, no eviction; exhaustion or
unsupported formats synchronize and use CPU fallback. Masked fills up to 256
pixels avoid individual GPU submit/wait overhead. Task pacing uses LVGL's next
timer deadline, keeping the original 16 ms refresh period.

The source and linked image enforce/preserve the actual LVGL FPS/CPU monitor.
Startup identifies `glyph-cache-v1`; a one-time text completion message checks
Nema wait completion against the hardware CLID. Optional `GPU_PROFILE=1` reports
DMA completion FPS, TE rate, glyph/cache counts and inclusive DWT timing totals.
See README_GPU.md for the profiling flash command and interpretation limits.

Additional passed host/static checks:
- 4,225 packed-A4 layouts (width/height 1..65), per-pixel values, odd-row padding,
  guard bytes, cache hits/alignment, table/arena exhaustion and input guards.
- All 780 glyphs / 457,432 pixels in the five local SDK fonts match their source
  bitmaps after row conversion; odd-width packed input explicitly exercised.
- Cached GPU path, synchronized CPU fallback, tiny-mask threshold, completion hook,
  16 ms refresh, FPS/GPU settings and adaptive task pacing checked structurally.
- Default and profiling configurations compile/link 201 units with no unresolved
  symbols. Default verification includes startup/completion text and cache code.

Profiling build (`make CONFIG=bin-profile GPU_PROFILE=1 -j8`):
- Binary/MRAM: 397,360 bytes; general shared SRAM: 428,288 bytes (81.69%).
- SHA-256: 3e334657572baee9c2bd7aee8f624230497e518871677b374b0e8a4024484b28.
- Initial SP 0x10061000; reset vector 0x46825; GPU IRQ 28 vector 0x43f15.

No hardware benchmark or sanitizer success is claimed. Host bitmap tests do not
emulate the GPU rasterizer. Neither 50–60 FPS nor reduced device CPU load is
established by compilation, completion counters or these tests.
