# Quiet Riot — Final demo firmware candidate (261104.3)

**Target:** STM32L476RG custom PCB, STM32CubeIDE 1.19.0, 48 kHz audio.  This is a complete editable CubeIDE project, *not* a guaranteed build/flashed/tested device.

## Build / import

1. Extract the complete ZIP and open the project root `261104-Digital_Hearing_Aid`.
2. On Windows run `PREPARE_PROJECT.bat`, then `VERIFY_FINAL.bat`. Use `REPAIR_AFTER_CUBEMX.bat` after any CubeMX regeneration. Recovery copies include the new `qr_demo_test` and `qr_sleep_policy` modules.
3. In STM32CubeIDE 1.19.0: File → Import → Existing Projects into Workspace, select project root; avoid importing an older workspace copy with the same name.
4. Project → Clean → Build. Confirm **a real ARM GNU ELF** and inspect the actual warnings, FLASH and SRAM sizes. The verification logs bundled here use cross-target Clang/LLD; they do **not** establish a successful production GNU/newlib link.
5. Flash only after confirming the correct PCB power rails and peripherals; start with the transducer on the bench, **not attached to or worn on a person**.

## Controls (only E8–E11 are wired/active in this firmware)

| Physical action | Firmware function | Details |
|---|---|---|
| Tap E8 | Bluetooth mode | Hold E8 1 s toggles BT mono ambience |
| Tap E9 | Telecoil mode | Hold E9 1 s toggles a continuous 1 kHz *bounded* test signal |
| Tap E10 | Mute / unmute | Short tap action is applied on release; hold E10 1 s toggles a repeating 250–4000 Hz chirp |
| Tap E11 | Microphone mode | Hold E11 1 s toggles mic rumble reduction |
| Hold E8 + E11 for 1.5 s | **SOFT SLEEP** | Output is immediately silent; AudioTask stops capture/speaker DMA, microphone stops; BT task requests disconnect and EDR disable |
| After sleep: remove hand, then approach proximity electrode for ≥120 ms | Wake | Restores selected audio input and requests BT profile/reconnect. The four touch pads and encoder are ignored while asleep. |
| Rotate encoder | Volume | Only outside sleep |

**Physical pad limitation:** Firmware cannot add separately wired MPR121 pads without a PCB change. E0–E7 are not available physical test buttons in this build. Functions above reuse long presses and one two-pad chord while retaining normal taps.

## Test tones and hearing safety

`qr_demo_test.c` generates a phase-continuous 1 kHz tone or recurring chirp in the SAI2B DMA refill callback. The **bench-only test generator is capped at 32,200 PCM counts** (about 98% of 16-bit full scale) with interpolated samples and controlled fade-in/out. This is a high *digital* level, **not a measured maximum electrical/acoustic power**. Do not use on the body; validate the driver, load and output SPL first. Cues are not mixed over a test signal.

Use an external reference/sensor to verify frequencies and spectral cleanliness. There is no guarantee of audible/physical output pressure from a PCM peak limit alone. Never test continuous 1 kHz or sweeps against a person's head or ear before an electroacoustic safety assessment. A tap of a different source key exits the test path.

## Telecoil changes and evidence

- **SAI2A is configured as a synchronous slave of SAI1A.** Both input and speaker output SAI use the PLLSAI1 clock source. The default telecoil renderer now consumes complete samples at 1:1 without the old elastic rate controller; this targets the repeating FIFO re-prime/popping mechanism.
- Microphone continues to use the elastic fallback, because DFSDM conversion is a different timing path.
- Automatic TC LEFT/RIGHT/MIX selection no longer changes every 128-frame block. A new candidate must remain consistent for 96 blocks (~256 ms) and contain measurable signal to be adopted.
- LOW24 full precision, the fourfold recovery cap, a non-gating speech-band filter, and source-off-only `TC HUM LEARN` remain. There is **no continuous notch chasing the wanted programme**.
- Periodic ADC interference may still be analogue, mains, sampling/clock, power or grounding related. Filtering alone cannot guarantee separation when unwanted and wanted frequencies overlap. Obtain actual raw `CAPTURE TC` and `TC PROBE` data to prove the cause.

### Bench-only UART diagnostics (when `G1_DIAGNOSTICS_UART_ENABLE=1`)

`TC OUTPUT EXACT` (default) and `TC OUTPUT ELASTIC` (comparison if physical clocks drift); `TC PROBE`; `CAPTURE TC` then `CAPTURE DUMP`; `TC HUM LEARN` with induction transmitter **off**, then compare `TC HUM ON` / `TC HUM OFF`; `SLEEP ON` and `SLEEP OFF` bench recovery; `DEMO 1K`, `DEMO CHIRP`, `DEMO STOP`.

The firmware will not learn a tone while programme audio is playing. A learned notch intentionally removes **both** the interference and any wanted audio at that specific frequency.

## Bluetooth 100% request — exact scope

On a BT401 connection (`TS+01`/`TS+02`) `BtTask`, the sole USART1 writer, now requests user-specified `AT+CA100` on **every connection and reconnection**, then queries `AT+QA`. This instruction is experimental: manufacturer documentation instead identifies **0–30 as local module/device volume**, not an independently verified way to force the iPhone/Android *media slider* to 100%. The phone may or may not synchronize through AVRCP Absolute Volume, depending on platform and module firmware. We do **not** claim to control the phone slider. Local digital output remains capped separately.

## What sleep means on this PCB

This is a **software-managed sleep**, not STM32 STOP/STANDBY. `ControlTask`, I2C1 proximity sampling, and the RTOS remain running so MPR121 proximity can wake it. The amplifier/audio path is stopped or zeroed. `BtTask` sends `AT+BA01` (disconnect), `AT+B300` (disable playback), `AT+B500` (disable EDR), `AT+CM08` (idle); on wake it restores mode and discovery. Some BT settings may require a power cycle, and there is no verified hardware BT power-enable gate. Therefore Bluetooth *RF-off / impossible-to-connect* **cannot be guaranteed** from software alone: hardware testing is mandatory. If the MPR121 is absent or proximity bit 12 never changes, recovery requires a bench console (`SLEEP OFF`) or power reset.

## Ownership model preserved

- **AudioTask P7:** sole owner of DMA start/stop, PCM capture, DSP, output FIFO; speaker SAI2B refill executes in DMA ISR.
- **ControlTask P6:** owns touch/encoder, the UI state, sleep/wake policy, and publishes audio requests.
- **BtTask P5:** sole owner of UART1 control commands and pairing state, including BT disconnect/re-enable and volume commands.
- **FreeRTOS queue:** 8 GroupEvent records transferred ControlTask → AudioTask, independent of shared source/gain request fields.
- **SRAM2:** 24 KiB dedicated FreeRTOS heap and separate 8 KiB capture scratch; source arrays for DMA in SRAM1.

## Physical acceptance checklist

On a bench with an external low-volume output monitor: clean native ELF build; cold startup; E8 BT connect, audio and volume; E9 TC 1 kHz induction input then transmitter off/noise observation; E11 microphone; tap E10 mute/unmute; hold E9 1 kHz and release/hold again to stop; hold E10 chirp and stop; verify source key exits test; E8+E11 sleep with sound immediately off and BT disconnect observation; release proximity, approach to wake; repeat tests with controls in unexpected orders. Inspect `speaker_underruns`, `tc_dma_overruns`, `service_deadline_misses` and capture clocks; if noise/popping remains, compare `TC OUTPUT EXACT` vs `TC OUTPUT ELASTIC` and collect raw PCM.

**Verification boundary:** Source cross-compile and portable PCM/logic tests are reproducible; real Bluetooth module behaviour, native CubeIDE ELF, PCM1808 noise suppression and acoustic output are untested on the user's device.
