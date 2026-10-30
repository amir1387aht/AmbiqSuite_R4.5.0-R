# Original watch application with Apollo4 Lite Nema GPU

This project runs the original `mb8_smartwatch` application/menu with SDK-matched
Ambiq LVGL 8.1.0 and Apollo4 Lite Nema GPU support. The original project is untouched.
There is a single application path: original menu, light sensor and accelerometer.
No alternate diagnostic UI, diagnostic timer, mode flag or submission/flush counter
is included.

## Original application settings

- `screen_menu_create()` is called unconditionally. Original menu source is unchanged.
- Light/accelerometer initialization and tasks always run, as in the original.
- `LV_USE_PERF_MONITOR=1`, positioned at `LV_ALIGN_BOTTOM_MID`: FPS and CPU overlay.
- All original Montserrat font options are restored (8 through 48, every even size).
- Original `LV_USE_LOG=0` and `LV_USE_MEM_MONITOR=0` are restored.
- The original commented `ui_init()` remains commented. UI layout was not redesigned.

## Build and flash

Use Arm GNU Toolchain 14.2.Rel1 (GCC 14.2.1) with its bin directory on PATH.
Build from this project's `gcc` directory with GNU make and POSIX-compatible shell
tools (for example Git Bash/MSYS2 on Windows):

```sh
make clean
make -j8
./flash.bat
```

Output: `gcc/bin/mb8_smartwatch_gpu.bin`, `.axf` and `.map`.
The batch file uses your original `make` plus
`C:\Users\Padidar\SEGGER\JLink_V812a\JLink.exe` invocation. A failed build stops
before programming. Keep your existing equivalent two-line batch file if preferred.
The J-Link command file loads only `bin/mb8_smartwatch_gpu.bin` at `0x00018000`,
then resets/runs. Original device `AMA4B2KP-KXR`, SWD, 4000 kHz are retained.
These are inherited watch settings, not independent silicon identification.

Running the script overwrites the application image. Keep a known-good firmware
and working SWD recovery access. No physical flashing was performed by the agent.

Source enumeration uses recursive GNU make wildcards, not shell find/sort;
parse-time checks catch missing application/LVGL sources before linking.

## GPU-specific changes that remain

This is not a byte-for-byte restoration of the old non-GPU build. The following
adaptations are necessary to keep the selected GPU implementation compatible:

1. SDK LVGL 8.1.0, Ambiq port archive, and Apollo4l Nema HAL are compiled together.
   The original LVGL 8.4 library is not compiled or mixed into this ABI.
2. Vendor porting ABI uses 32-bit LVGL color. Two aligned BGRA8888 draw buffers
   render into shared SRAM; completed regions are converted into a persistent
   full-panel RGB565 staging frame. On LVGL's last flush, one contiguous dirty-row
   range is sent through the unchanged MSPI/Raydium panel driver. Generated image bytes/descriptors were
   adapted to BGRA8888 without changing their source colors or alpha.
3. Nema allocators are initialized before sensor/panel DMA. Setup stack, task
   allocation checks, GPU interrupt/completion handling and DAXI flushing remain.
4. The LVGL task owns GPU/panel transitions. Sleep/wake and brightness requests
   are coordinated at frame/DMA boundaries rather than interrupting GPU work.
5. A FreeRTOS-derived millisecond tick drives LVGL. GPU power is retained; automatic
   deep-sleep/power cycling is not qualified and has not been restored blindly.
6. Existing packed fonts stay packed in MRAM. Direct GPU font-atlas rendering is
   not enabled; LVGL's CPU glyph fallback and supported GPU blending are used.
   Five local SDK font copies and small wrappers for the other incompatible SDK
   descriptors reconcile SDK const declarations without changing font bitmap data.

Shared SDK files are not modified. No EVB display shield, NemaDC/DSI transport, or
external PSRAM configuration is imported. Unused legacy library/reference assets
remain available in `../mb8_smartwatch` instead of being duplicated here.

## Display pipeline performance correction

The original application had full-height RGB565 draw buffers. The initial GPU
port used two 64-line BGRA buffers but still called the TE-gated panel transfer
for every partial flush. A full-height refresh therefore queued eight separate
panel transfers, each waiting for another TE pulse. LVGL also lacked a display
wait callback and spun while transfers were pending. These are transport
serialization defects, not evidence that the GPU itself takes that long to draw.

The corrected pipeline:

1. Keep the small GPU draw buffers, but copy each completed area into a separate
   persistent 192 x 490 RGB565 staging frame.
2. Immediately release non-final partial draw buffers after copying, without
   starting panel DMA or consuming a TE pulse.
3. Use `lv_disp_flush_is_last()` to submit one full-width dirty-row range for the
   complete refresh. Union dirty rows across multiple invalid areas; preserve
   previously displayed pixels outside modified rectangles.
4. The unchanged original panel driver waits for TE once for this transfer.
5. Keep the final LVGL flush pending until the DMA callback completes. LVGL's
   dedicated display wait callback blocks on an ISR-signaled binary semaphore
   instead of burning CPU in its flushing loop. The MSPI IRQ priority permits
   FreeRTOS FromISR calls. GPU and panel completion semaphores are independent.

The staging frame is initialized before use and is never modified while DMA is
reading it. DAXI is flushed before DMA. Narrow invalidations can transmit extra
columns because rows are sent full-width, but untouched rows are not transmitted.
No menu/widget/font/monitor/flash-script changes accompany this correction.

This fixes identified source-level serialization. On-device FPS, visual integrity,
CPU load and power are not measured by host tests. The standard LVGL CPU overlay
uses handler busy/idle timing, which includes time spent blocked inside the
handler; it is not a direct FreeRTOS CPU-utilization measurement. Its calculation
has not been changed to make the displayed result look better.

## Memory layout

| Region | Start | Reserved/available size |
| --- | --- | --- |
| Application MRAM | 0x00018000 | 1,998,848 bytes |
| CPU TCM | 0x10000000 | 256 KiB |
| LVGL host TCM pool | 0x10040000 | 128 KiB reserved |
| General shared SRAM | 0x10060000 | 512 KiB |
| LVGL GPU shared pool | 0x100e0000 | 512 KiB reserved |

The general shared region includes a 4 KiB MSP stack, 16 KiB C heap, draw buffers,
188,160-byte RGB565 staging frame, and FreeRTOS heap. Current linked usage of
that 512 KiB general shared region is 425,088 bytes (81.08%). Linker reservations/assertions prevent pool
collisions. External allocation requests use the initialized shared pool, not PSRAM.

Required vendor archives (already present in the updated SDK):

- `third_party/LVGL/ambiq_support/gpu_lib_apollo4/gcc/bin/lvgl_ambiq_porting.a`
- `third_party/ThinkSi/config/apollo4l_nemagfx/gcc/bin/lib_nema_apollo4l_nemagfx.a`

## Regression checks

From this project's root, with the Arm toolchain on PATH:

```sh
python3 tests/test_app_default.py
python3 tests/test_source_discovery.py
python3 tests/test_flash_config.py
python3 tests/test_display_pipeline.py
python3 tests/validate_port.py
cc -std=c11 -Wall -Wextra -Werror -Ilibs/gpu_port tests/test_rgb565.c -o /tmp/test_rgb565
/tmp/test_rgb565
cc -std=c11 -O2 -Wall -Wextra -Werror -Ilibs/gpu_port tests/test_framebuffer.c -o /tmp/test_framebuffer
/tmp/test_framebuffer
python3 tests/verify_firmware.py
```

The firmware verifier checks the default `gcc/bin` directory, or accepts another
output-directory name as its first argument. It verifies actual linked FPS/CPU
monitor text, GPU interrupt vector, memory/buffer addresses and unresolved symbols.

Compile/link and host/static checks are not hardware validation. Verify original
menu behavior, bottom-middle FPS/CPU text, colors, touch, sensors, brightness,
scrolling and sleep/wake on the watch. Performance and power measurements remain
on-device work; the LVGL overlay is not an independent GPU-utilization benchmark.
