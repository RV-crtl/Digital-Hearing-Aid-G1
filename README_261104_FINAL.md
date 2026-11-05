> **Firmware QR261104.2:** For the latest source/controls/sleep/test-tone instructions, first read [`README_QUIET_RIOT_DEMO_FINAL.md`](README_QUIET_RIOT_DEMO_FINAL.md). This older page is retained as background documentation.

# Quiet Riot — Digital Hearing Aid

**STM32CubeIDE 1.19.0 / STM32L476RG, operating firmware project**

The importable project is `261104-Digital_Hearing_Aid` (keep the project name exact).
The contents of `Core`, `Drivers`, `Middlewares`, `.project`, `.cproject`, the `.ioc`,
`STM32L476RGTX_FLASH.ld` and `Recovery` form the buildable firmware. `Tests`, `Tools`,
`Dashboard`, `Documentation`, `Assessment` and `Hardware` are development assets, not
additional runtime tasks; the firmware builds only the configured translation units.

## Build and test from a fresh extraction

1. Extract the ZIP fully to a short path, e.g. `C:\STM32\QuietRiot\`.
2. Close CubeIDE and run **`PREPARE_PROJECT.bat`**. This checks and restores
   source parity after CubeMX regeneration. It does not flash the device.
3. Run **`VERIFY_FINAL.bat`** (optional portable tests; the ARM cross-check in this
   package uses Clang/LLD if available, not the installed STM32 GCC toolchain).
4. Open CubeIDE 1.19.0 → File → Import → Existing Projects into Workspace.
   Select the extracted folder and import **`261104-Digital_Hearing_Aid`**.
   Remove/close an older imported copy first to avoid building the wrong source.
5. Project → Clean → Build Project. Confirm Debug/261104-Digital_Hearing_Aid.elf
   was **newly created**, with 0 errors and 0 warnings before flashing.
6. Flash the PCB and follow `Documentation/GROUP_DEMO_15_MINUTE_PLAN.md` using
   the intended standalone battery supply. Test initially via a bench speaker.

**Do not press CubeMX Generate Code after PREPARE without rerunning PREPARE.**
The C drivers and HAL ISR owners are recovered deliberately; regenerating the IOC
can otherwise introduce duplicate DFSDM/I2C drivers or overwrite the boot paths.

## Last reported build failure and correction

- User's Debug log had GCC 13.3 link failure:
  `FreeRTOS heap must be exactly 24 KiB`.
- The original GNU ld `SIZEOF(.freertos_heap_ram2)==24576` assertion is replaced
  by two *hard* SRAM2 MEMORY regions: `RAM2_HEAP` at `0x10000000` (24 KiB) and
  `RAM2_CAPTURE` at `0x10006000` (8 KiB). Physical overlap is impossible under
  the linker memory-region constraints.
- The 8-KiB linker scratch start is now declared in C as
  `extern uint8_t _capture_ram2_start[];`, not as a one-byte scalar.
  This corrects the two GCC -O2 array-bounds warnings.
- Stale project-name check (`G1_Product_R26_...`) now accepts the real 261104 project.
- The firmware's signal chain, microphone gain and R26.6-style telecoil filtering
  have NOT been changed by these repairs.

See `BUILD_FIX_AND_VERIFICATION.md` for what was verified and the outstanding
hardware/native-ELF checks.

## Physical user controls

| Input | Action |
| --- | --- |
| E8 tap | Bluetooth source |
| E9 tap | Telecoil source |
| E10 tap | Mute/unmute |
| E11 tap | Microphone source |
| E8 hold | Optional mono room effect (Bluetooth only) |
| E11 hold | Optional microphone rumble reduction |
| Rotary encoder | Digital volume with IRQ and polling fallback |

## Architecture / important caveats

- **AudioTask P7** owns real-time audio processing, routing and output FIFO.
- **ControlTask P6** owns the physical UI / commands; some changes are shared
  scalar requests, and `g_group_events` is an eight-entry event queue.
- **BtTask P5** owns BT401/USART1 pairing and control commands.
- PCM1808 BT/telecoil: 24-bit stereo receive in 32-bit SAI DMA words;
  telecoil uses LOW24 full-precision recovery. MP34DT05 PDM mic uses DFSDM.
- A single mono listening signal is duplicated into 16-bit stereo SAI2B frames.
- `ConnectivityService` manages BT/telecoil automatic state decisions; MIC is an
  explicitly selected input rather than a third equal-priority wireless source.
- ROOM is a mono ambience effect; microphone rumble reduction is not ANC.
- Source-level tests cannot prove clear telecoil sound, stability under all
  physical conditions, acoustic output limits or the final native GNU link.
