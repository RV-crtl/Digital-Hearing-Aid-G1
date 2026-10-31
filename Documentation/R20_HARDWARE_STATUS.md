# R20 hardware baseline and repository integration

**Date:** 31 October 2026 · **Target:** STM32CubeIDE 1.19.0 · **MCU:** STM32L476RG

## Board results reported by the developer

The separate **G1 Product R20 Battery Standalone** project was reported to work for the most part. In the R17–R20 development series, microphone capture, speaker output, Bluetooth playback, physical rotary encoder, and the MPR121 touch controls were independently reported working. Telecoil playback and true battery-only/without-ST-LINK cold startup were reported problematic and remain **unverified**.

These observations relate to the separately built R20 firmware, **not automatically to this Git SHA**.

## Changes staged in this repository

- Import the hardware-neutral R20 source-selection, mute, volume, command, and E8–E11 touch policy into Core/Inc/g1_user_controls.h and Core/Src/g1_user_controls.c.
- Bring the reusable configurable quadrature-decoder detent threshold to group_encoder.
- Fix the R20 serial command parser so VOLUME 100 followed by CRLF is accepted.
- Add an executable host regression and GitHub Actions job.

**Important integration boundary:** The checked-in legacy demo_app.c is still the old source-integrated baseline. The full R20 board driver (g1_product_board), interrupt/DMA ownership, CubeMX .ioc, boot fallback, and Bluetooth/telecoil source switching have **not been transplanted by this patch**. Do not claim this commit is a flashable reproduction of the R20 hardware build. Keep the separately tested R20 ZIP until a complete CubeIDE 1.19.0 GCC build and a custom-board cold-boot test have been performed on the consolidated repository.

## Next release gates

1. Promote the complete R20 hardware driver, its matching headers, and the exact CubeIDE project metadata together; preserve third-party middleware licences and existing website/assessment/PCB material.
2. Re-run full ARM GCC compile and **final executable link**, not only syntax/relocatable checks; flash and validate against the same Git SHA.
3. Confirm power-off → battery power-up with no ST-LINK or UART adapter, operating touch/encoder, mic, Bluetooth, mute and audio output.
4. Independently verify telecoil signal, clock/DMA capture, routing and final speaker output.
5. Measure safe gain/output levels, supply stability and resetting under load before any user-worn demonstration.

Host tests establish only deterministic C policy behaviour; they do not prove bootloader, battery hardware, audio safety, or connected-device operation.
