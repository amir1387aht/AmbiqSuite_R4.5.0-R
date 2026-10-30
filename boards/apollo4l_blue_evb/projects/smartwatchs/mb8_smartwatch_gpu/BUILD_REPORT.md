# Original application + GPU: build report

SDK baseline: `db7b6b12e57861d05b85966cdfa0dae3a28c02e7`.
Toolchain: Arm GNU Toolchain 14.2.Rel1 / GCC 14.2.1,
Cortex-M4, FPv4-SP-D16, hard-float ABI.

## Current configuration

One application only: original menu, light sensor and accelerometer, with the
Apollo4 Lite Nema GPU integration and SDK LVGL 8.1.0. Original monitor/font/log
settings are restored: FPS/CPU monitor enabled at bottom-middle, all original
Montserrat sizes enabled, LVGL logging disabled, memory monitor disabled.
No alternate UI, diagnostic timer/counters or mode selection is compiled.

## Clean build result

`make -j8` in `gcc` successfully compiled 200 translation units and linked the
actual vendor GPU libraries. No unresolved ELF symbols remain. Existing SDK/watch
compiler warnings remain; this is not a warnings-free claim.

| Item | Value |
| --- | --- |
| Binary | gcc/bin/mb8_smartwatch_gpu.bin |
| Binary/MRAM bytes | 394,848 |
| SHA-256 | 3384a6717a1eb269b0413d0e610f228b94735497a37906de900f8a43c3e41fb7 |
| Initial SP | 0x10061000 |
| Reset vector | 0x460e5 |
| GPU IRQ 28 vector | 0x437d9 |
| CPU TCM used | 4,888 bytes |
| General shared SRAM used | 261,504 bytes |
| Additional reserved LVGL TCM pool | 128 KiB |
| Additional reserved LVGL GPU shared pool | 512 KiB |

## Passed checks

- Default application always calls original screen_menu_create(); original menu,
  main entry point and accelerometer code remain unchanged.
- Light/accelerometer initialization/tasks are unconditional.
- All original font settings, FPS/CPU monitor position, and logging settings match
  the original lv_conf.h. Linked binary contains the actual LVGL FPS/CPU format text.
- Source enumeration works without a shell: 26 application sources, 130 remaining
  SDK LVGL sources, plus local port/font override sources, for 200 units total.
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
GPU font-atlas acceleration and production deep-sleep integration remain deferred.
GPU-specific color ABI, memory layout, initialization, DMA synchronization and tick
adaptations remain; this is not a byte-identical build of the non-GPU application.
