# Build and verification report

SDK baseline: `db7b6b12e57861d05b85966cdfa0dae3a28c02e7`.

Toolchain: Arm GNU Toolchain 14.2.Rel1, GCC 14.2.1; Cortex-M4 / FPv4-SP-D16 / hard-float.

Both configurations were built cleanly from source. Each compiled 200 translation units and linked with the uploaded vendor archives. No unresolved symbols remain. SDK/watch compiler warnings exist; this is not a warnings-free or hardware-tested claim.

| Configuration | Binary bytes | Initial SP | Reset vector | GPU IRQ 28 vector |
| --- | ---: | --- | --- | --- |
| smoke | 274,244 | 0x10061000 | 0x47435 | 0x44b9d |
| app | 415,976 | 0x10061000 | 0x48085 | 0x45775 |

## SHA-256

- **smoke**: `acebf8129fdf61ae8fa8516b6ebbcb373e04ae3edde70c1d6fef6bfcabc96aae`
- **app**: `bc201622bcd24e7c4a02ce1bd2c110409d6b30180cbe090f5fa6ce61dd699de1`

Vendor archives:
- `lvgl_ambiq_porting.a`: `b5dff43858880e45315ca4a13e5afa168bcfd4266695d565324db998649825b6`
- `lib_nema_apollo4l_nemagfx.a`: `9620164c81b1120f9eb70249e709fa488d6c42f89278c95c1abfc5375a1659e1`

## Passed checks

- HAL and BSP compiled locally during bring-up; uploaded GCC GPU archives match ARMv7E-M/VFPv4-D16/hard-float ABI.
- Both firmware images clean-compile/link; real Nema/porting libraries used, no stubs.
- ELF symbol tables contain no unresolved symbols; GPU vector resolves to the strong SDK GPU ISR, not the default handler.
- MSP is aligned and in its reserved stack region; reset vector points at Reset_Handler.
- Draw/transport buffers are 32-byte aligned in shared SRAM; TCM/SSRAM allocator reservations match linker symbols.
- Dependency files reference the SDK v8.1 fork, not copied LVGL 8.4.
- All 65,536 RGB565 colors round-trip through the actual transport conversion function.
- All 12,577 converted image pixels preserve source RGB565 colors and alpha.
- All 18,528 possible horizontal invalidation ranges obey even-column rounding and panel limits.
- All 1,258 tracked files under the original watch project remain unchanged against the updated SDK baseline.

## Not established

No watch, debug probe, display emulator or hardware GPU execution was available. Boot success, panel timing/colors, GPU results, touch/sensor behavior, runtime heap/stack margins, frame rate, power and sleep/wake require the hardware acceptance procedure in README_GPU.md. Direct GPU font-atlas rendering and production low-power management are deferred.

## Follow-up: Windows source enumeration

Replaced shell `find`/`sort` with recursive GNU make wildcards for both application
and SDK LVGL source lists and application include directories. Added parse-time
guards for main.c, rtos.c and lv_obj.c so missing sources fail before linking.
The regression test discovers 26 application C files, 144 SDK LVGL C files and
19 application include directories with SHELL set to a nonexistent executable.
Both configurations were rebuilt with GCC 14.2.1; firmware hashes and sizes are
unchanged from the results above. Both ELF validation runs passed. Native Windows
execution was not available; POSIX build recipes still need Git Bash/MSYS2 tools.
The SDK signedness warning is separate from the missing-source link failure.
