# Build and hardware troubleshooting

## Why the uploaded 4 November 16:41 build failed

The decisive error is `region RAM overflowed by 3560 bytes` while linking `.bss`; it is **not** an encoder-polling compile warning. There were zero compiler warnings in the supplied log. The earlier R26.7 SRAM2 move also overlapped the capture recorder with the RTOS heap: unsafe, even if the linker had succeeded.

This release allocates SRAM2 *exclusively* using the linker: `ucHeap` at `0x10000000` size `0x6000` and capture at `0x10006000` size `0x2000`. Check the **actual GNU `.map`** for `ucHeap` in `.freertos_heap_ram2`, `_capture_ram2_start`, `.bss`, and free SRAM1. The included LLVM cross-link checks layout only; actual GNU/Newlib .elf remains necessary before flashing.

### If the same 3560-byte RAM error returns

1. Confirm the project at the top of the CubeIDE console is **`261104-Digital_Hearing_Aid`**, not the old G1/R26 name. Check path: never compile the old project still present in your workspace.
2. Run `PREPARE_PROJECT.bat`; inspect `Core/Inc/FreeRTOSConfig.h` (`configAPPLICATION_ALLOCATED_HEAP=1`, heap **24576**) and `STM32L476RGTX_FLASH.ld` (has both SRAM2 sections). Match `.ioc` to `24576` too.
3. Delete the project's generated `Debug` and `Release` directories **only**, then do `Project > Clean` and rebuild (do not remove source). Clean the imported project rather than an old launch target.
4. Open the generated GNU `.map`; look for `ucHeap` in `.bss` instead of `.freertos_heap_ram2`, and inspect which object is consuming RAM.
5. If it still fails, return the full new linker output and `.map`. The ARM Clang/LLD verification is not a replacement for the real CubeIDE GCC link.

### Encoder fallback warning

The original product implements an IRQ decoder and low-rate `G1_BoardPoll` fallback. An 'unused polling' symbol in a different development image may be harmless, but verify physical detents in both directions. This revision leaves encoder hardware logic unchanged.

### Group-demo audio troubleshooting

- LOW24 telecoil is retained from the audible R26.4 path. `TC HUM LEARN` requires source-off/no wanted audio; fixed notch cannot separate signal and noise at the same frequency.
- `SPATIAL OFF` and `NOISE OFF` return the effect path to baseline. `ANC ON` is intentionally rejected.
- An induction source and speaker amplifier need correct shared grounding, power filtering, I2S frame alignment and clocking. Do not confuse a periodic ADC coupling issue with a DSP gating issue.
- Watchdog/recovery functions must be verified on the PCB; source-level tests do not prove no glitches/drop-outs or hearing-safe SPL.
