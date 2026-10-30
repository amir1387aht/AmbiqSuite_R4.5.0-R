# Apollo4 Blue Lite: Mi Band 8 LVGL/Nema bring-up

## Status and boundaries

**Both configurations clean-compile and link successfully.** Tested with Arm GNU
Toolchain 14.2.Rel1 (`arm-none-eabi-gcc` 14.2.1), Cortex-M4, hard-float ABI.
No hardware was connected: boot, panel colors, rendering correctness, touch,
performance, power consumption and sleep/wake still require testing on the watch.
This is a first GPU bring-up, not a production-qualified firmware release.

This isolated source fork is based on `../mb8_smartwatch`. The original project is unchanged.
The SDK baseline used for the final builds is `db7b6b12e57861d05b85966cdfa0dae3a28c02e7`.
The original project's `libs/lvgl` **8.4 tree is NOT compiled**.
The repository upload omits duplicate unused 8.4 sources, binary reference assets
and `ex` documentation; these remain available in `../mb8_smartwatch` and the
previous full-copy handoff ZIP. All files needed to build this port are included. The makefile uses
`third_party/LVGL/lvgl`, Ambiq's SDK-matched **8.1.0** fork. Do not substitute the
current 9.3 Ambiq GitHub branch or mix its libraries with this build.

## Build

Run these commands from **this project's gcc directory** after putting the Arm
GNU toolchain's `bin` directory on PATH:

```sh
# Default: controlled GPU smoke screen, sensor tasks disabled.
make all GPU_SMOKE_TEST=1 CONFIG=bin-smoke -j8

# Original watch menu and sensor tasks, using the GPU-capable LVGL stack.
make all GPU_SMOKE_TEST=0 CONFIG=bin-app -j8
```

### Windows source-discovery fix

The makefile now recursively enumerates sources and application include directories
with GNU make's own `wildcard` function. It no longer calls shell `find` or `sort`.
On Windows, the previous enumeration could silently omit all application and LVGL
sources, leading to undefined `main` and `vApplicationStackOverflowHook` at link.
Parse-time checks now stop immediately if core application or LVGL sources are absent.
Existing recipes still require a POSIX-compatible shell and utilities (for example,
Git Bash/MSYS2); this does not add native cmd.exe recipe support.

After updating, use a clean rebuild from the gcc directory:

```sh
make clean CONFIG=bin
make all GPU_SMOKE_TEST=1 CONFIG=bin -j8
```

The SDK `am_hal_card.h` signedness warning is unrelated to those missing symbols;
it is not a linker error. Shared SDK source is intentionally unchanged.

Each output directory contains `mb8_smartwatch_gpu.axf`, `.bin`, and `.map`.
Plain `make` builds the smoke configuration in `gcc/bin`.
The separate output directories make the two configurations easy to identify.
A build-mode stamp also forces recompilation when changing GPU_SMOKE_TEST in the
same output directory. The project-level Makefile forwards to `gcc`.
HAL and BSP archive build rules call their actual `gcc` directories directly.

The exact GCC archives required from AmbiqSuite R4.5.0 are:

- `third_party/LVGL/ambiq_support/gpu_lib_apollo4/gcc/bin/lvgl_ambiq_porting.a`
- `third_party/ThinkSi/config/apollo4l_nemagfx/gcc/bin/lib_nema_apollo4l_nemagfx.a`

These were uploaded into the SDK repository and used in the successful builds.
`make check-libs` detects their absence; no substitute/stub libraries are used.
`GPU_LIB=/path/to/...` and `NEMA_LIB=/path/to/...` override their locations.

## Rendering architecture

1. Initialize SRAM power configuration, UART and clocks in the setup task.
2. Initialize LVGL's TCM/shared heaps and Nema in `watch_gpu_init()` **before any
   sensor/panel DMA**. Apollo4l HAL DMA entry points can indirectly call nema_init.
3. Keep the GPU USER power claim held during bring-up; enable the Apollo4l Nema
   interrupt handler at GPU IRQ 28 through the SDK HAL. The setup task has an
   8 KiB stack and task-creation results are checked.
4. Render into two 192 x 64 **BGRA8888** shared-SRAM buffers. The vendor porting
   library's supplied configuration uses LV_COLOR_DEPTH=32. This is a library ABI
   requirement; it is not a limitation of the GPU hardware's pixel formats.
5. Wait for command-list completion, convert the completed region into a separate
   RGB565 transport buffer, flush CPU bus writes, then use the original asynchronous
   MSPI/Raydium driver. Retain the original 24-column panel offset and even-column
   area alignment. Only the DMA completion callback releases the flush buffer.
6. Use the FreeRTOS tick as the LVGL millisecond timebase.

No EVB NemaDC/DSI display path or external-PSRAM initialization is imported.
The default LVGL menu is preserved in application mode; the SquareLine `ui_init()`
call remains commented out, as it was in the original project.

### Fonts and images: deliberately staged

- Existing packed fonts remain packed; `LV_AMBIQ_FONT_REALIGN_BITMAP=0`.
- Direct GPU glyph rendering is deliberately rejected. LVGL reads packed glyphs
  on the CPU and its supported blending primitives can still use Nema. Full GPU
  text/atlas support is a later milestone, not claimed by this port.
- The five enabled Montserrat fonts have local SDK copies to reconcile inconsistent
  SDK const declarations and retain their packed bitmap bytes in MRAM.
- The five generated SquareLine images in this sibling were converted from
  RGB565+A8 to BGRA8888, including descriptor data_size updates. Original images
  were not changed. Tests verify color/alpha preservation for all 12,577 pixels.
- GPU texture operations reject inputs outside initialized shared SRAM, allowing
  the normal LVGL CPU fallback for MRAM assets rather than asserting or handing
  an inaccessible address to Nema. This port has no initialized external memory.

## Memory layout

| Region | Address | Capacity | Role |
| --- | --- | --- | --- |
| MRAM | 0x00018000 | 1,998,848 bytes | Code/constant data; original application origin |
| CPU TCM | 0x10000000 | 256 KiB | Initialized CPU data |
| LVGL TCM pool | 0x10040000 | 128 KiB | LVGL host/object allocator |
| CPU/shared area | 0x10060000 | 512 KiB | MSP, C heap, RTOS heap/BSS, draw and DMA buffers |
| LVGL GPU pool | 0x100e0000 | 512 KiB | Nema command lists, ring buffer, masks/textures |

The pools are explicitly reserved NOLOAD linker regions, not overlapping magic
addresses. The SDK's three-heap allocator is overridden **inside this project**
so there is no access to the EVB's assumed 0x14000000 PSRAM. External-allocation
requests use the actual shared pool, preserving allocator identity for free/realloc.
The local TLSF copy sizes its internal indexes for the largest enabled pool.
The C heap is a real reserved 16 KiB interval, not an unreserved fraction of SRAM.
Shared zero-initialized resource arrays are included in the startup BSS clearing.

Final linked MRAM use: smoke **274,244 bytes**, menu **415,976 bytes**.
Regular shared-region use: smoke **261,152 bytes**, menu **261,504 bytes**.
These figures exclude no sections: the two separate LVGL pool regions above are
additionally reserved at their full capacities. Runtime heap high-water usage has
not been measured. See BUILD_REPORT.md for firmware hashes and verification.

## Task ownership and power

The LVGL task owns rendering and panel operations. Other tasks request sleep/wake
at safe frame boundaries instead of suspending the LVGL task during command-list
construction or DMA. Brightness requests are queued and applied by that same owner.
The driver waits for DMA/GPU completion before deinitializing the panel.

The GPU is deliberately kept powered. The FreeRTOS sleep hook uses normal sleep
if invoked; existing tickless-idle remains disabled. **Battery life/deep-sleep
optimization is not implemented or validated.** Do not enable automatic GFX power
cycling until command-list/DMA ownership and retained state have been tested.

## Hardware acceptance procedure

Back up a known-good firmware and confirm recovery/SWD access first. Review the
existing watch-specific bootloader, image-origin and flashing requirements before
writing anything. No flashing was performed or automatically enabled here.
The copied flash.bat is disabled; the JLink command file names only the GPU binary.

1. Start with `GPU_SMOKE_TEST=1`, not the sensor-enabled application.
2. Confirm the UART initialization message and no LVGL/Nema/heap error.
3. Confirm GPU IRQ 28 reaches the strong `am_gpu_isr` implementation.
4. Verify red, green, blue and white bars; an orange animated rounded rectangle;
   and readable text. Check channel order, byte order, clipping, bottom/right edges.
5. Observe UART counters increasing for submitted command lists, successful wait
   operations and completed panel flushes. These are diagnostics, not an FPS result.
6. Exercise touch and leave animation running. Watch for stalls, artifacts,
   watchdog resets, allocation failures and stack-overflow hooks.
7. Then build application mode. Check scrolling, sensor initialization, brightness,
   panel off/on, and first frame after waking. Measure heap/stack headroom on-device.
8. Only after these pass should GPU glyph atlases, GPU RGB565 conversion,
   deep-sleep/power gating, and a later LVGL upgrade be attempted.

The provided .bin files are **compile/link tested only**, not hardware-approved.

## Reproduce non-hardware checks

From this project directory:

```sh
python3 tests/test_source_discovery.py
python3 tests/validate_port.py
cc -std=c11 -Wall -Wextra -Werror -Ilibs/gpu_port tests/test_rgb565.c -o /tmp/test_rgb565
/tmp/test_rgb565
python3 tests/verify_firmware.py
```

The image verifier needs arm-none-eabi-nm on PATH and both bin-smoke/bin-app builds.
It checks unresolved symbols, initial SP/reset vector, the actual GPU interrupt
vector, 32-byte shared-buffer alignment, heap boundaries and exclusion of LVGL 8.4.
These checks cannot emulate the GPU or establish runtime drawing correctness.

## Local SDK overlays

`libs/gpu_port` contains the only adapted SDK copies; shared SDK source is untouched:

- `lv_mem.c`, `lv_tlsf.c`: no-PSRAM memory model.
- `lv_gpu_ambiq_nema.c`: memory-access/font fallbacks, fail-fast init/waits, counters.
- `am_bsp.h`: omit EVB generic-panel declarations that conflict with the watch's
  customized Raydium types; the BSP implementation remains the SDK implementation.
- Five font sources: packed MRAM bitmaps and compatible public font declarations.
- `watch_gpu.*`, `watch_tick.h`, `watch_rgb565.h`: bring-up, smoke UI, clock and transport.

The base watch's 8.4 library and unused auxiliary assets remain in the original
project for reference and are not compiled. CppProperties/.vscode settings name
this project, not the original.

## References reviewed

- SDK `boards/apollo4b_evb_disp_shield/examples/graphics/lvgl_test`
- SDK `third_party/LVGL/ambiq_support` and `third_party/ThinkSi/config/apollo4l_nemagfx`
- Root `Apollo4-Graphics-Getting-Started-Guide.pdf`, especially sections 3.1/3.2,
  4.2/4.3 and 4.9. Its NemaDC/DSI examples are not this watch's MSPI transport.
- [AmbiqMicro/LVGL](https://github.com/AmbiqMicro/LVGL), default revision
  fcb05b7a8dbeb4743c019d7065ec94bc07962ffd; inspected v8.1 branch too.
- [AmbiqMicro/lv_port_ambiq](https://github.com/AmbiqMicro/lv_port_ambiq), revision
  be2dfdd522f7897c819c422d319d1ee3949fab95; Apollo4l HAL and memory/driver design.

Newer references were used for comparison, not to replace the SDK-matched stack.
See licenses/ for applicable notices; upstream licensing restrictions still apply.
