# Mi Band 8 GPU smoothing: build and validation report

Toolchain: Arm GNU Toolchain 14.2.Rel1 / GCC 14.2.1, Cortex-M4,
FPv4-SP-D16, hard-float ABI. Firmware links the real Ambiq LVGL and Apollo4l Nema
archives; no host mocks or stubs are linked into firmware.

## Changes in this follow-up

- The project-local watch_nema_hal.c retains the SDK Apollo4l HAL except for
  initializing the RTOS ISR wake flag and yielding on both completion paths after
  publishing last_cl_id/callback. The SDK binary-semaphore branch omitted yield.
  Only the local HAL is compiled, not a second SDK nema_hal.o. GPU IRQ priority
  remains the SDK default 4, compatible with the watch's FreeRTOS syscall limit.
- The LVGL task now only captures and overwrites a one-element profiling queue.
  Formatting and blocking UART transmission run on a lower-priority logger task,
  with its own buffer and a task-context UART mutex. Graphics does not wait for
  the UART queue. The logger has a 4 KiB stack inside the existing RTOS heap.
- gap1/gap2/gap3p count submitted-frame start gaps in TE periods. During active
  scrolling they reveal skipped presentation opportunities; idle gaps are normal.
- flash.bat forwards make arguments, so it supports GPU_PROFILE=0/1 directly.
  The separate profiling batch file is removed. J-Link command and failure gate
  are preserved; FPS/CPU monitor stays enabled in either configuration.
- No changes to menu layout, touch protocol/coordinates, scroll physics, 16 ms
  timers, GPU conversion, double panel buffering or requested accelerometer stop.
  Original project and shared SDK source files remain unchanged.

## Compile/link results

| Configuration | Units | Binary/MRAM bytes | CPU TCM bytes | General shared SRAM bytes |
| --- | --- | --- | --- | --- |
| bin | 203 | 396,804 | 4,888 | 428,320 |
| bin-profile | 203 | 431,096 | 6,312 | 428,480 |
| bin-cpu | 203 | 430,232 | 6,312 | 428,448 |

All initial stack pointers: 0x10061000. Zero unresolved symbols in each ELF.
The GPU vector resolves to the strong ISR from the local Apollo4l HAL:
- `bin`: reset `0x46825`, GPU IRQ 28 `0x1cca9`.
- `bin-profile`: reset `0x46f65`, GPU IRQ 28 `0x1d165`.
- `bin-cpu`: reset `0x46d95`, GPU IRQ 28 `0x1cf95`.

Binary SHA-256:
- `bin`: `831433078c86baf39104fed4fa2060ce5b42ca24b2c3a5bc3953af0cf0565300`
- `bin-profile`: `73963c1c7010f6fb771e8fd5cd5fa4c55a0f6c7c9fbd768c0e26eca27b12ffe9`
- `bin-cpu`: `373284808d2a29498209720e6bf251ce693715d45f965ef8e2d4cb9ef37471ac`

The profiling build is larger because the lower-priority logger uses newlib
snprintf into its own buffer rather than the SDK formatter's shared global
buffer. Default release excludes the logger task, UART mutex and continuous
profile output. GPU shared heap (512 KiB), LVGL host pool (128 KiB), 96 KiB glyph
arena, second panel frame and conversion CL reservations are unchanged.
Runtime RTOS task/queue/mutex allocations use the already-reserved RTOS heap;
they are not a new memory region. Allocation failures are checked.

Existing SDK/watch compiler warnings remain; no warnings-free claim is made.

## Passed checks

- Fresh default build in bin-clean is byte-identical to bin and passes ELF
  verification.
- Default, profiling and CPU-conversion control builds compile/link with 203
  translation units and no LVGL 8.4 sources or external PSRAM dependency.
- Linked FPS/CPU overlay, startup marker, vectors, static shared buffers, reserved
  pool boundaries and local-HAL object selection verified. Diagnostic logger
  symbols are present only when its GPUprof format string is linked.
- Actual ISR function body executed with host register/RTOS mocks: semaphore,
  notification and bare-metal configurations; initialized flags and completion
  publication before conditional ISR-exit yield verified.
- Production profiler producer/consumer executed with host RTOS/UART mocks:
  producer performs no formatting/UART, single-slot overwrite is bounded, output
  task is lower priority than LVGL, and cadence counters format correctly.
- TE cadence helper covers initial/consecutive/skipped/same-edge samples and
  unsigned counter wrap. Double-frame ownership/order integration checks pass.
- Conversion host tests use production C and SDK API headers with mocked Nema
  execution, covering strides/alignment, rectangles, snapshots, completion-ID
  reset side effect and safe CPU fallbacks. These are not GPU raster tests.
- All 65,536 RGB565 reference colors, 12,577 migrated asset pixels, 18,528 panel
  invalidation ranges, 4,225 A4 layouts and 780 real-font glyphs/457,432 pixels
  pass existing conversion/font regression tests.
- Original app/menu/font/monitor/log settings preserved; only the previously
  requested accelerometer task stop differs from its original source.
- All 1,258 original-project tracked files remain unchanged.
- Flash-script argument forwarding and requested wrapper removal checked
  structurally; Windows/J-Link execution remains untested by the agent.

## Hardware evidence and limits

The user reported 53 FPS on the overlay with the preceding build. The supplied
active-scrolling profile commonly showed 46-49 completed DMA transfers/s while
TE stayed near 60 Hz, GPU conversion enabled and CPU glyph fallback zero. Average
one-second totals do not establish individual-frame cadence or prove why every
visible hitch occurs. Source inspection identified the missing ISR yield and
inline blocking diagnostic output as concrete scheduling problems to address.

An example supplied 223-byte profile line represents about 19.4 ms at 115200-8-N-1
on the wire; FIFO buffering may reduce its actual blocking duration. The new
handoff removes that transmission from the graphics task, not all diagnostic CPU
or bus overhead. First compare feel with ./flash.bat GPU_PROFILE=0 (FPS overlay
still on); capture counters with ./flash.bat GPU_PROFILE=1 if needed.

No physical watch, GPU simulator or J-Link probe was available. Compilation and
host tests do not establish new-build FPS, frame cadence, stock-equivalent touch
physics, colors, power or sleep/wake behavior. Verify on the same scrolling task.
The technical integration walkthrough is in PORTING_GUIDE.md.
