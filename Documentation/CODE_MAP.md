# Firmware source map — QR261104.8

**Canonical active source** is `Core/Src/`; public API headers are `Core/Inc/`. Only one `Core/Src/main.c` and one `261104-Digital_Hearing_Aid.ioc` are retained. There is no `Recovery/` mirror or additional embedded project. [Read the complete step-by-step function guide](FIRMWARE_EXECUTION_WALKTHROUGH.md).

| When execution reaches… | Source implementation | Key responsibilities |
| --- | --- | --- |
| Board reset | [`main.c`](../Core/Src/main.c) | HSE/HSI fallback, PLL, HAL/GPIO/DMA/USART/SAI/board init, native RTOS startup |
| RTOS startup | [`rtos_bootstrap.c`](../Core/Src/rtos_bootstrap.c) · [`demo_app.c`](../Core/Src/demo_app.c) | AudioTask P7, ControlTask P6, BtTask P5 and notifications |
| DMA/serial peripheral init | [`sai.c`](../Core/Src/sai.c) · [`dma.c`](../Core/Src/dma.c) · [`usart.c`](../Core/Src/usart.c) | SAI1A Bluetooth, SAI2A telecoil, SAI2B speaker, USART1 BT401 control |
| BT PCM decode | [`pcm_transport.c`](../Core/Src/pcm_transport.c) · [`demo_app.c`](../Core/Src/demo_app.c) | 24-bit stereo ADC input, captured frames, level analysis, mono mix |
| Telecoil weak-signal restoration | [`tc_s24_recovery.c`](../Core/Src/tc_s24_recovery.c) | Full-width 24-bit signal capture and bounded gain |
| Telecoil speech filtering | [`tc_cleanup.c`](../Core/Src/tc_cleanup.c) | Gentle high pass, fourth-order 3.4 kHz low pass, optional fixed notch |
| Telecoil idle comfort | [`tc_idle_squelch.c`](../Core/Src/tc_idle_squelch.c) | Background learning, coherence test, −30 dB quiet idle; not in-band ANC |
| PDM microphone | [`g1_product_board.c`](../Core/Src/g1_product_board.c) | DFSDM decimation, DMA capture queue, touch and encoder initialization |
| Mic AGC and scene features | [`g1_ambient_dsp.c`](../Core/Src/g1_ambient_dsp.c) · [`g1_scene_ml.c`](../Core/Src/g1_scene_ml.c) | Ambient level conditioning, bounded gain, unsupervised clustering |
| Source modes / automatic route | [`connectivity_controller.c`](../Core/Src/connectivity_controller.c) · [`connectivity_service.c`](../Core/Src/connectivity_service.c) | BT, TC, MIC, AUTO, OFF policy |
| Shared audio DSP | [`demo_app.c`](../Core/Src/demo_app.c) · [`audio_dynamics.c`](../Core/Src/audio_dynamics.c) · [`group_features.c`](../Core/Src/group_features.c) | Optional HPF/EQ/compression, volume, PCM FIFO |
| Speaker output | [`speaker_output.c`](../Core/Src/speaker_output.c) · [`demo_app.c`](../Core/Src/demo_app.c) | FIFO, left/right I²S slots, SAI2B DMA, MAX98357 |
| Controls | [`g1_user_controls.c`](../Core/Src/g1_user_controls.c) | Encoder/touch selection, volume, mute |
| Debug and scope | [`demo_app.c`](../Core/Src/demo_app.c) | USART2 optional diagnostics, PCM RMS/peak, TC status, scope and FIFO underruns |

## Directory policy

- `Core/Src`: **all project-owned firmware implementation**, not historical mirrored copies.
- `Core/Inc`: corresponding application APIs and required custom-board HAL compatibility declarations.
- `Drivers/`, `Middlewares/`: STM32 HAL/CMSIS and FreeRTOS sources; not renamed or relocated because the project relies on their conventional include paths.
- `Documentation/`, `Hardware/`, `Tests/`, `Validation/`, `Tools/`, `Assessment/`, `docs/`: **repository-only** engineering collateral, never a second firmware image.
- `261104-Digital_Hearing_Aid.ioc`, `.project`, `.cproject`, `STM32L476RGTX_FLASH.ld`: required project metadata.

## Timing and accuracy boundaries

128 frames at 48 kHz take 2.667 ms; DMA callbacks copy completed halves, while AudioTask owns filter states and fills the 4096-sample mono FIFO. ControlTask publishes user changes without resetting a DSP block inside an ISR. Bluetooth analogue output from BT401 reaches the MCU via PCM1808; USART1 carries commands and status only. Telecoil noise and programme overlap cannot be separated by simple scalar gain. Source-level/Clang checks do not prove a newly built CubeIDE ELF, measured acoustic limits or sufficient PCB input SNR.
