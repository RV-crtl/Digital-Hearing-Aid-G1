# R26.10 cross-subsystem interface contracts

[← Subsystem index](README.md) · [Anna](ANNA_CONTROLS.md) · [Ryan](RYAN_CONNECTIVITY.md) · [Mariya](MARIYA_AUDIO_DSP.md) · [Naomi](NAOMI_OUTPUT.md)

**Purpose:** tell the lecturer not only which modules are linked, but **what is passed, who owns it, when it is valid, how often it changes, what happens on failure and how to test it**. Based on the QR261104.10 CubeIDE ZIP. The GitHub firmware tree remains behind the documented release; do not use stale source alone to verify newly added functions.

## 0. Shared startup contract

```text
Power/reset
  -> Core/Src/main.c::main()
  -> HAL_Init() -> clocks, GPIO, DMA, USART, SAI
  -> G1_BoardInit() [Anna I2C1/encoder; Mariya DFSDM]
  -> Demo_StartNativeRTOS()
  -> BootstrapTask() [FreeRTOS priority 3]
  -> Demo_AppInit() [UI, routing, DSP, group queue]
  -> Demo_ControlTask() [raises priority to 6]
  -> create_child_tasks()
       -> StartAudioTask() [priority 7, input DMA + DSP + FIFO producer]
       -> StartBtTask() [priority 5, BT401 UART]
```

There are **three production FreeRTOS tasks**, not four tasks for four students. The SAI/DFSDM TX/RX completion code executes in **interrupt context**, not in a speaker task. Native FreeRTOS task creation is the authority; the QR261104.10 `.ioc` *describes* corresponding objects for CubeMX visibility. Do not generate a duplicate task scheduler.

**Contract source:** `Core/Inc/g1_rtos_contract.h`. ControlTask and AudioTask own separate state. The application-level objects initialize before DMA begins. Board initialization failures are recorded; not all input failures are fatal.

## 1. Anna controls → AudioTask / Naomi

| Property | Actual contract |
|---|---|
| Producer | `G1_BoardPoll()` → `G1Ui_Touch()` / `G1Ui_Encoder()` → `product_ui_commit()` |
| Consumer | `StartAudioTask()` applies source/routing; `speaker_fill_region_isr()` reads mute/output gain |
| Payload | `G1UiState` and changed-flag mask; `DemoControl_t` request fields; Q15 gain |
| Enum | AUTO=0, BT=1, TC=2, OFF=3, MIC=4 |
| Trigger | Touch/encoder event or optional USART2 command, not PCM-sample clock |
| Initialization | `G1Ui_Init()` in `Demo_AppInit()`; default MIC |
| Synchronization | ControlTask publishes request flags; AudioTask single-writer for DMA/DSP state |
| Error | Invalid commands rejected; touch errors/reprobe counted; source restarts re-prime audio |

**Why:** ControlTask must not call `HAL_SAI_DMAStop()` in the touch callback or mutate telecoil filter memory mid-block. AudioTask consumes `g_r28_route_restart_pending`, resets routing and chooses a new topology. This directly links Anna's E9 press to Ryan's TC receiver and Naomi's speaker.

## 2. Ryan BT/TC SAI DMA → AudioTask

| Property | Actual contract |
|---|---|
| Producer | `HAL_SAI_RxHalfCpltCallback()` / full for SAI1A Bluetooth or SAI2A telecoil |
| Consumer | `capture_pop_bt()` / `capture_pop_tc()` in AudioTask |
| Data | 256 `uint32_t` words = **128 interleaved stereo 24-bit frames** |
| Rate | Nominal 48 kHz; one half-block ≈ **2.667 ms** |
| Buffers | Separate `g_bt_capture[8]` and `g_tc_capture[8]`; 7 usable slots each |
| Sync | `__DMB()` before publishing head index; `vTaskNotifyGiveFromISR()` |
| Ownership | DMA fills active half → ISR copies completed half → task owns ring snapshot |
| Failure | Ring full → increment `bt_dma_overruns` / `tc_dma_overruns`, drop new block |
| Init | SAI/DMA configured in startup; task exists before RX DMA started |

**Task notifications are not PCM queues.** Audio bytes live in the custom rings; the notification only wakes AudioTask. Such rings should not be falsely represented as FreeRTOS message queues in CubeMX.

## 3. Mariya DFSDM → AudioTask

| Property | Actual contract |
|---|---|
| Producer | DFSDM Filter0 DMA1 Ch4 half/full callback → `mic_push()` |
| Consumer | `G1_MicTakeBlock()` in AudioTask |
| Hardware | PC2 clock/PB14 data; DFSDM1 Channel 2, Filter 0, Sinc3, oversampling 40 |
| Data | 128 × `int32_t` raw DFSDM samples → **128 signed `int16_t` mono PCM** |
| Rate | 1.92 MHz PDM /40 = 48 kHz PCM; 2.667 ms per half |
| Buffers | `s_mic_queue[8][128]`, 7 usable slots |
| Synchronization | `__DMB()` + `Demo_NotifyMicReadyFromISR()` |
| Ownership | ISR writes ring; AudioTask copies and DC-blocks before AGC |
| Failure | `mic_overflow`/`mic_errors`; initialization retry backoff of ~1 s |
| Init | `G1_MicReinit()` at boot; `G1_MicStart()` when MIC mode selected |

**MIC mode has SAI input topology mask 0**, but DFSDM **is active separately**. That is not a contradiction. The R26.10 `Core/Src/dfsdm.c` wrapper exposes the init entry point; the hardware HAL setup remains in `g1_product_board.c`.

## 4. Ryan connectivity service → selected mono PCM

| Property | Actual contract |
|---|---|
| Control calls | `ConnectivityService_ApplyCommand()`, `ConnectivityService_Step()`, `Connectivity_Update()` |
| Data calls | `ConnectivityService_ProcessBluetoothBlock()` / `ConnectivityService_ProcessTelecoilBlock()` |
| Input | Signed 16-bit mono PCM block, sample count, timestamps and source activity |
| Output | Selected/eligible audio PCM or zeros, plus source/mute/status |
| Owner | AudioTask owns service/filter state |
| Error | Bad args/time/state → error result; stale BT data can be discarded |
| Init | `ConnectivityService_Init()` before source processing |

**Manual TC exception:** explicit manual TC can bypass AUTO's source-qualification test, while AUTO TC can be filtered by `TelecoilFilter_ProcessSample()` before the later TC cleanup. **Connected BT401 status** on USART1 does not prove sound is arriving through SAI1A.

## 5. All selected sources → shared DSP

| Property | Actual contract |
|---|---|
| Producer | `process_bt_pcm()`, `process_tc_pcm()` or MIC drain in `StartAudioTask()` |
| Consumer | `process_selected_audio(pre, source)` |
| Data | **128 signed int16 mono PCM frames**, nominal 48 kHz |
| Source IDs | TC=1, BT=2, MIC=3 |
| Processing | Optional `AudioDynamics_ProcessBlock()`, group profile and effects |
| Owner | AudioTask single writer of DSP history and processed samples |
| Error | Bound/clip inside DSP; source change resets DSP/FIFO state |
| Initialization | `AudioDynamics_Init()`, `GroupFeatures_Init()` in `Demo_AppInit()` |

**Actual FreeRTOS queue:** `g_group_events = xQueueCreate(8, sizeof(GroupEvent))`, where `GroupEvent` contains two `uint8_t` fields (`type` and `value`). `group_event_send()` uses `xQueueSend(..., 0U)`; `group_audio_update()` consumes via `xQueueReceive()`, so only AudioTask modifies group DSP state. The queue registers as `groupEvents` for RTOS debugging. It is the real queue declared in the QR261104.10 `.ioc`.

## 6. AudioTask → Naomi SAI2B output DMA

| Property | Actual contract |
|---|---|
| Producer | `output_queue_push()` in AudioTask |
| Consumer | `speaker_fill_region_isr()`, called from SAI2B TX DMA callbacks |
| Stored data | `g_output_samples[4096]` of signed **int16 mono** PCM |
| Output | Two I²S slots with each mono sample duplicated into left/right |
| Rate | 48 kHz; one completed TX DMA half ≈ 2.667 ms |
| Sync | One writer/one ISR reader, monotonic indices and `__DMB()` |
| Prime | 1,024 samples (~21.3 ms) normal BT/TC; 2,048 (~42.7 ms) MIC |
| Overflow | Producer rejects new 128-frame block, `speaker_queue_overruns++` |
| Underrun | Very short decaying-block concealment, then silence and re-prime |
| Init | `SpeakerOutput_StartDma()` before normal input playback |
| Hardware | SAI2B / DMA1 Ch7 → MAX98357 → bone conductor |

**Important:** there is no separate speaker task. The SAI2B DMA interrupt refills the safe TX half, allowing output to continue without waiting for a newly scheduled FreeRTOS task. Normal BT/TC use exact 1:1 FIFO consumption; microphone uses a slow fractional read to absorb independent clock drift.

## 7. Slow communications and diagnostics versus real-time audio

| Interface | Owner | Content | Timing / failure |
|---|---|---|---|
| USART1 BT401 | Ryan BtTask + ISR | AT commands and `TS+` status; **not audio** | Async RX bytes + periodic status query; UART overrun/error |
| USART2 console | ControlTask + ISR | Optional commands, metrics, scope transport | Diagnostic build only; separate 256-byte RX ring |
| I²C1 touch | Anna ControlTask | MPR121 bitmap/proximity | ~15 ms touch poll; errors and re-probe |
| Encoder EXTI | Anna ISR | Gray-code transitions → signed detents | Edge events; bad-edge count |
| Audio RX DMA | Ryan/Mariya drivers | PCM blocks, not text commands | Real-time 2.667 ms half-block cadence |
| Speaker TX DMA | Naomi | PCM samples, not UART telemetry | Real-time 2.667 ms refill cadence |

## 8. Who diagnoses the symptom first?

| Symptom | First investigation | Next subsystem |
|---|---|---|
| E9 press does nothing | Anna: `touch_raw`, `touch_errors`, changed flags | Ryan: mode and SAI2A DMA |
| Phone connects but BT is silent | Ryan: `TS+` link **versus** SAI1A PCM RMS | Naomi: selected source and output FIFO |
| MIC selected but silent | Mariya: DFSDM readiness, DMA callback and `mic_blocks` | Anna mute; Naomi output |
| TC programme has hiss | Ryan: raw source-off/on SNR and recovery gain | Naomi: rule out output underruns |
| Speaker clicks | Naomi: underrun/concealment/queue depth | Ryan/Mariya: input drops or DSP delay |
| Touch sluggish with audio active | Anna: polling/callback rate | Shared: AudioTask priority and fairness delay |
| Source switch replays old audio | Shared: `output_queue_reset()` and restart flags | Naomi: FIFO read/prime state |

## 9. Interface review checklist

Before modifying a cross-member interface, answer all of these: **What event starts it? What exact type/units/format pass through it? Who owns the buffer before and after handoff? What happens if the consumer is late? What initializes first? How does it report failure? What downstream regression test verifies the change?**

**For the viva:** the strongest combined demonstration is *Anna E9 event → Ryan SAI2A TC capture/recovery → shared DSP → Naomi FIFO/DMA → physical output*, then compare *Mariya E11 → DFSDM mic capture/AGC → same output*. This proves you understand the complete hardware and software system.

[← Subsystem index](README.md)
