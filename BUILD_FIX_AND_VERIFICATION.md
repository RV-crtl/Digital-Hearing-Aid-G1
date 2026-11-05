# 261104 build repair — evidence and limits

## User's actual 2026-11-05 Debug build errors

1. `ld.exe: FreeRTOS heap must be exactly 24 KiB` (link fails).
2. `demo_app.c` line 618 `-Warray-bounds`: writing past an apparent one-byte linker symbol.
3. `demo_app.c` line 503 `-Warray-bounds`: `memcpy` beyond an apparent one-byte linker symbol.

## Corrections

- `STM32L476RGTX_FLASH.ld`: split SRAM2 into two disjoint MEMORY regions
  RAM2_HEAP (24 KiB @ 0x10000000) and RAM2_CAPTURE (8 KiB @ 0x10006000).
  Each output section is bound to its own region and cannot spill across.
  Removed the brittle GNU ld exact-size `SIZEOF` assertion that failed in GCC 13.3.
- `Core/Src/demo_app.c`: pointer to linker scratch is based on a correctly
  declared unbounded array `_capture_ram2_start[]`. Capacity is already
  limited to 8192 bytes by compile-time static asserts and linker section size.
- `Tools/verify_active_project.py`: replace obsolete R26 project name.
- `Core/Src/freertos.c`: correct the stale description of lab-only workers.
- `Recovery` restores the corrected linker and source files after CubeMX.

## Architecture decisions retained

- No change to the known recoverable R26.4 LOW24 telecoil audio path and the
  R26.6 conservative cleanup or controlled source-off hum calibration.
- No change to high-sensitivity microphone path or Bluetooth control/audio.
- Audio and control tasks remain different priorities by design; DMA moves the
  high-rate sample stream while control events travel through a separate
  FreeRTOS queue and shared requests. This is not a non-uniform design defect.
- No additional fake CPU stress tasks in the normal operating firmware.

## Verification evidence

- `Verification/QUIET_RIOT_SOURCE_CHECKS.txt`: complete source+portable regression report.
- `Verification/QUIET_RIOT_ARM_MEMORY.txt`: ARM Clang/LLD memory-layout smoke test.
- `Verification/QUIET_RIOT_ARM_ALL_SOURCE.txt`: all 77 source objects ARM
  cross-compiled and linked with unresolved libc symbols explicitly ignored.
- `Tools/verify_261104_release.py` runs the same checks in a suitable tool environment.
- `Tools/verify_active_project.py` confirms CubeIDE metadata and required drivers.

**Limits:** The container did not have the Windows STM32CubeIDE ARM GNU/newlib
compiler or your physical PCB. Clang/LLD is not a substitute for the complete
CubeIDE 1.19 GCC `Debug/*.elf` link. Rebuild in CubeIDE and use the black-box
acceptance checklist before declaring demo readiness.

## Windows clean-build troubleshooting

If CubeIDE still prints the old `FreeRTOS heap must be exactly 24 KiB` message,
then it is building an **old linker script**. Check Project → Properties →
C/C++ Build → Settings → MCU GCC Linker → General → Linker Script, which must
be `STM32L476RGTX_FLASH.ld` in THIS imported project. Search the active linker
file for `RAM2_HEAP` and `RAM2_CAPTURE`. Close the old workspace project and
Clean the new project, then rebuild.
