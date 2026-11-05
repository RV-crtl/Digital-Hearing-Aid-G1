# R26 Firmware: Power-on to Bone-Conduction Output

**Scope:** application-level walkthrough of Quiet Riot QR261104.8. This document is maintained at repository level, not inside the STM32CubeIDE import package. Paths refer to the active project under Core/. The source links are useful for navigation; verify that the checked-in code matches the release before relying on implementation-specific details.

## 1 — Power-on, reset flags and clocks

**File:** [Core/Src/main.c](../Core/Src/main.c). **Functions:** `main()`, `SystemClock_Config()`, `PeriphCommonClock_Config()`.

```c
int main(void) {
  s_g1_boot_reset_csr = RCC->CSR;   /* Preserve the hardware reset cause */
  __HAL_RCC_CLEAR_RESET_FLAGS();    /* Prevent stale reset flags */
  HAL_Init();                       /* HAL initialization and system tick */
  Demo_EarlyInit();                 /* Early application-level setup */
  SystemClock_Config();            /* Configure 48 MHz core clock */
  PeriphCommonClock_Config();      /* Peripheral clock tree */
  MX_GPIO_Init(); MX_DMA_Init();    /* Pins and DMA infrastructure */
#if G1_DIAGNOSTICS_UART_ENABLE
  MX_USART2_UART_Init();           /* Optional bench/debug console */
#endif
  MX_USART1_UART_Init();           /* Bluetooth control UART */
  MX_SAI1_Init(); MX_SAI2_Init();   /* Audio capture and speaker interface */
  G1_BoardInit();                   /* Microphone, touch and encoder */
  Demo_RuntimeStatsTimerInit();    /* Profiling timer */
  Demo_StartNativeRTOS();          /* Starts scheduler; does not return */
  while (1) {}
}
```

This is an annotated *abridgement* of the real entry point: it intentionally omits boot-reporting and error-check branches. On a standalone battery build, USART2 can be disabled while USART1 remains available for Bluetooth commands. The production entry point is **Core/Src/main.c only**.

## 2 — Hardware/peripheral initialization

| Signal | Hardware | Driver and relevant function |
|---|---|---|
| Bluetooth audio | Phone → BT401 analogue output → PCM1808 | [Core/Src/sai.c](../Core/Src/sai.c): `MX_SAI1_Init()`; SAI1A + DMA2 Ch1 |
| Telecoil audio | Magnetic coil → MCP6232 front-end → PCM1808 | [Core/Src/sai.c](../Core/Src/sai.c): `MX_SAI2_Init()`; SAI2A + DMA1 Ch6 |
| Microphone | MP34DT05TR-A digital PDM mic | [Core/Src/g1_product_board.c](../Core/Src/g1_product_board.c): `G1_MicReinit()`, `G1_MicStart()`; DFSDM + DMA1 Ch4 |
| Bluetooth module control | BT401 | [Core/Src/usart.c](../Core/Src/usart.c): `MX_USART1_UART_Init()` |
| Optional bench console | USART2 | [Core/Src/usart.c](../Core/Src/usart.c): `MX_USART2_UART_Init()` |
| Bone-conduction output | SAI2B → MAX98357 → transducer | [Core/Src/speaker_output.c](../Core/Src/speaker_output.c): `SpeakerOutput_Init()`, `SpeakerOutput_StartDma()` |

## 3 — RTOS tasks, DMA handoff and buffering

**File:** [Core/Src/demo_app.c](../Core/Src/demo_app.c). `Demo_StartNativeRTOS()` establishes the application scheduler. `StartAudioTask()` (priority 7) performs real-time block processing; the control task (priority 6) handles user commands/settings; `StartBtTask()` (priority 5) monitors Bluetooth UART.

```c
/* Execution trace: separate calls/events, not literal compilable C */
HAL_SAI_RxHalfCpltCallback(...) / HAL_SAI_RxCpltCallback(...)
  -> capture_push_bt_from_isr(...) or capture_push_tc_from_isr(...)
  -> capture_pop_bt(...) or capture_pop_tc(...)
  -> process_bt_words(...) or process_tc_words(...)
```

The DMA handlers copy the completed half-buffer into the appropriate queue and notify AudioTask; they do not run intensive filters in the interrupt. Each DMA half contains 128 stereo frames, or 2.667 ms at 48 kHz. The two audio ADC streams each use 24-bit samples carried in 32-bit words.

## 4 — Bluetooth decoding

**Files:** [Core/Src/demo_app.c](../Core/Src/demo_app.c), [Core/Src/pcm_transport.c](../Core/Src/pcm_transport.c), [Core/Src/bt_profile.c](../Core/Src/bt_profile.c), [Core/Src/qr_bt_volume.c](../Core/Src/qr_bt_volume.c).

`process_bt_words()` calls `PcmTransport_DecodeStereoWordsMode()` to sign-interpret ADC words and reduce 24-bit stereo capture to signed 16-bit left/right PCM. The selected mono mix is normally (left + right)/2. `process_bt_pcm()` measures RMS/peak-to-peak for activity and routes the selected programme to `process_selected_audio(..., 2U)`.

```c
/* Representative stereo downmix equation, not a verbatim source block */
int32_t mono = ((int32_t)left + (int32_t)right) / 2;
```

`BtProfile_Apply()` configures the Bluetooth module via UART. `QrBtVolume_Init()` and `QrBtVolume_ConnectedEdge()` track new connections so module volume can be reapplied after reconnection. **Bluetooth UART carries commands/status, not streaming PCM.**

## 5 — Telecoil: weak-signal recovery and noise control

**Files:** [Core/Src/demo_app.c](../Core/Src/demo_app.c), [Core/Src/tc_s24_recovery.c](../Core/Src/tc_s24_recovery.c), [Core/Src/tc_cleanup.c](../Core/Src/tc_cleanup.c), [Core/Src/tc_idle_squelch.c](../Core/Src/tc_idle_squelch.c).

**`TcS24Recovery_Init()`** clears recovery state; **`TcS24Recovery_SetCap()`** bounds allowed gain; **`TcS24Recovery_Process()`** sign-extends 24-bit ADC words and applies bounded gain before reducing samples to 16-bit. A plain 24→16-bit shift may lose weak programme detail; gain does not itself improve signal-to-noise ratio.

**`process_tc_words()`** selects mono audio from the recovered channels, based on level and selection policy. **`TcCleanup_Reset()`** initializes filter memory; **`TcCleanup_Block()`** applies low-frequency rejection and a fourth-order 3.4 kHz low-pass filter. **`TcCleanup_SetNotchAuto()` / `TcCleanup_SetNotchFrequency()`** control an optional narrow interference notch.

**`TcIdleSquelch_Init()`**, **`TcIdleSquelch_Relearn()`** and **`TcIdleSquelch_Enable()`** manage the idle suppressor; **`TcIdleSquelch_Process()`** computes block RMS and correlation, learns a noise reference, qualifies activity and smoothly attenuates the idle signal by approximately 30 dB.

```c
/* Representative, excerpted detection decisions from tc_idle_squelch.c */
const bool energy = (rms > baseline + baseline/3U + 24U);
const bool weak_tone = tone && rms > baseline + baseline/12U + 4U;
const bool candidate = energy || weak_tone;
/* Persistent activity opens the path; idle decays with a smooth gain ramp. */
```

**Important:** Manual TC and AUTO TC can pass through different filter chains. In AUTO routing, [Core/Src/connectivity_service.c](../Core/Src/connectivity_service.c) can call [Core/Src/telecoil_filter.c](../Core/Src/telecoil_filter.c) before the common cleanup. Active noise that overlaps speech remains a hardware/SNR challenge.

## 6 — Microphone: PDM capture and ambient gain

**Files:** [Core/Src/g1_product_board.c](../Core/Src/g1_product_board.c), [Core/Src/g1_ambient_dsp.c](../Core/Src/g1_ambient_dsp.c), [Core/Src/demo_app.c](../Core/Src/demo_app.c).

`G1_MicReinit()` configures the DFSDM channel/filter; `G1_MicStart()` arms the DMA path; `G1_MicTakeBlock()` delivers PCM and reduces DC offset. `product_mic_condition()` invokes `G1Ambient_Process()`. `G1Ambient_Init()` resets its state. `G1Ambient_Process()` computes features including RMS and peaks, applies bounded automatic gain, and updates scene observations. This is lightweight adaptive processing, not neural-network speech separation.

## 7 — Connectivity and source arbitration

**Files:** [Core/Src/connectivity_controller.c](../Core/Src/connectivity_controller.c), [Core/Src/connectivity_service.c](../Core/Src/connectivity_service.c).

`Connectivity_DefaultConfig()` supplies defaults; `Connectivity_Init()` initializes the controller; `Connectivity_SetEnabled()` changes enable state; `Connectivity_Update()` applies detection, timing and route transitions; `Connectivity_GetOutputs()` retrieves the decision. On the service side, `ConnectivityService_Init()`, `ConnectivityService_Step()`, `ConnectivityService_ApplyCommand()` and `ConnectivityService_SetGains()` coordinate modes and levels. `ConnectivityService_ProcessBluetoothBlock()` and `ConnectivityService_ProcessTelecoilBlock()` connect sample activity to routing. Supported user modes include BT, TC, MIC, AUTO and OFF.

## 8 — Shared DSP and speaker FIFO

**Files:** [Core/Src/demo_app.c](../Core/Src/demo_app.c), [Core/Src/audio_dynamics.c](../Core/Src/audio_dynamics.c), [Core/Src/group_features.c](../Core/Src/group_features.c).

```c
/* Short, annotated excerpt of process_selected_audio(); other checks omitted */
if (g_demo_control.dsp_enabled) {
  AudioDynamics_ProcessBlock(&g_dynamics, pre, g_demo_processed_mono,
                             DEMO_FRAMES_PER_HALF, g_demo_diag.muted != 0U);
} else if (g_demo_diag.muted) {
  memset(g_demo_processed_mono, 0, sizeof(g_demo_processed_mono));
} else {
  memcpy(g_demo_processed_mono, pre, sizeof(g_demo_processed_mono));
}
/* Guarded in the full implementation by the group bypass flag. */
GroupFeatures_Process(&g_group_features, g_demo_processed_mono,
                      g_demo_processed_mono, DEMO_FRAMES_PER_HALF);
DhaEffects_Process(g_demo_processed_mono, DEMO_FRAMES_PER_HALF, source);
output_queue_push(g_demo_processed_mono);
```

`process_selected_audio()` measures the selected input, resets state when source changes, applies optional dynamics/profile/effects and records DSP metrics. `output_queue_push()` places mono samples in the 4096-sample FIFO. `output_queue_pop_exact_isr()` consumes clock-locked BT/TC samples; `output_queue_pop_async_isr()` compensates small microphone clock drift. `scale_output_sample()` applies digital volume and soft limiting. `speaker_fill_region_isr()` refills DMA with stereo-framed duplicate mono and provides limited underrun concealment/fades.

## 9 — Final output and fault diagnosis

**File:** [Core/Src/speaker_output.c](../Core/Src/speaker_output.c). `SpeakerOutput_Init()` prepares the SAI output; `SpeakerOutput_StartDma()` begins continuous DMA transfer; `SpeakerOutput_StopDma()` stops it; `SpeakerOutput_IsInitialized()` reports status. SAI2B sends 16-bit stereo-formatted I²S to the MAX98357 amplifier driving the transducer.

Bench commands in `demo_app.c` include `AUDIO PATH`, `BT CHECK`, `TC CHECK`, `TC PROBE`, `TC 24 STATUS`, `TC CLEAN`, `TC IDLE STATUS`, `TC FILTER ON/OFF`, `TC IDLE ON/OFF`, and `MIC STATUS`. For noise diagnosis, compare ADC recordings with telecoil source off/on and check both input SNR and speaker underrun counters.

## 10 — Exact function locator

| Function | File | Why to inspect |
|---|---|---|
| `main`, `SystemClock_Config` | `Core/Src/main.c` | Power-on, clocks and scheduler launch |
| `MX_DMA_Init`, `MX_SAI1_Init`, `MX_SAI2_Init` | `Core/Src/dma.c`, `Core/Src/sai.c` | Peripheral capture timings |
| `capture_push_bt_from_isr`, `capture_push_tc_from_isr` | `Core/Src/demo_app.c` | DMA-to-task handoff |
| `process_bt_words`, `process_tc_words` | `Core/Src/demo_app.c` | Source-specific block decoding |
| `TcS24Recovery_Process` | `Core/Src/tc_s24_recovery.c` | Weak 24-bit telecoil recovery |
| `TcCleanup_Block` | `Core/Src/tc_cleanup.c` | Speech-band filters and notch |
| `TcIdleSquelch_Process` | `Core/Src/tc_idle_squelch.c` | Idle-noise detector/fade |
| `G1_MicTakeBlock`, `G1Ambient_Process` | `Core/Src/g1_product_board.c`, `Core/Src/g1_ambient_dsp.c` | DFSDM microphone DSP |
| `Connectivity_Update` | `Core/Src/connectivity_controller.c` | Route state transitions |
| `process_selected_audio`, `output_queue_push` | `Core/Src/demo_app.c` | Shared DSP and buffering |
| `SpeakerOutput_StartDma` | `Core/Src/speaker_output.c` | I²S amplifier transmission |

**Directory contract:** Keep one active `Core/Src/main.c`, one `.ioc` and its matching `.project`/`.cproject`. `Core/Inc` holds headers and `Core/Src` holds implementations; generated HAL/FreeRTOS support stays under `Drivers` and `Middlewares`. Engineering docs/scripts may be kept at repository level but should not be included in the minimal CubeIDE distribution.