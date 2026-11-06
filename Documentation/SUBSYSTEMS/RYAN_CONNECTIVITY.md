# Ryan — Bluetooth, telecoil and connectivity subsystem

**Follow this in execution order:** peripherals ready → BtTask status → user chooses input → audio DMA starts → interrupt publishes frames → AudioTask decodes the chosen source → connectivity policy → shared DSP → Naomi's output. This page explicitly separates **Bluetooth CONTROL** (USART1) from **Bluetooth AUDIO** (analogue + PCM1808 + SAI1A).

[← Subsystem index](README.md) · [Original 39-event trace](../FIRMWARE_EXECUTION_GUIDE_R26.md) · [Interface contracts](INTERFACES.md)

> **Baseline:** QR261104.10 full CubeIDE release. GitHub main currently lacks some newer R26.10 sources such as `tc_s24_recovery.c`, `tc_idle_squelch.c` and `qr_bt_volume.c`; they are present in the complete release ZIP. The source excerpts below describe that release.

## Ryan's physical interfaces and software responsibility

| Physical input | Actual signal | STM32 peripheral | Who consumes it |
|---|---|---|---|
| BT401 module control | AT commands + `TS+` status bytes | **USART1**; 256-byte RX ring | **BtTask** (priority 5) |
| BT401 module audio | Analogue stereo L/R → **PCM1808 ADC** | **SAI1A** RX, DMA2 Channel 1, 24-bit stereo transport | **AudioTask** (priority 7) |
| Induction-loop telecoil | Magnetic induction → **MCP6232 analogue front-end** → **PCM1808 ADC** | **SAI2A** RX, DMA1 Channel 6, 24-bit stereo transport | **AudioTask** |
| Source-selection policy | State/activity/enable flags, not a physical bus | `connectivity_service.c` / `connectivity_controller.c` | AudioTask |

**Do not say USART1 carries the Bluetooth sound.** It only carries control/status. Audio bytes come from SAI1A DMA. Similarly, SAI1A provides shared clock timing needed for SAI2A telecoil reception; this is why the TC topology starts both SAI receiver blocks.

```text
main() → MX_USART1_UART_Init(), MX_SAI1_Init(), MX_SAI2_Init()
    │
BootstrapTask → Demo_AppInit() → ConnectivityService_Init()
    ├→ StartBtTask()             [BT401 UART status/AT control]
    │   └→ poll_bt_uart()        [read TS+ responses from ISR ring]
    └→ StartAudioTask()          [DMA topology and PCM DSP]
        ├→ BT RX ISR → g_bt_capture[] → process_bt_words()
        └→ TC RX ISR → g_tc_capture[] → process_tc_words()
            ├→ TcS24Recovery_Process() [24-bit recovery]
            ├→ channel selection / mono
            └→ process_tc_pcm()       [manual vs AUTO routing]
                ├→ TcCleanup_Block()
                ├→ TcIdleSquelch_Process()
                └→ process_selected_audio() ─→ Naomi's FIFO
```

## R0. Boot: initialize the interfaces, not the actual audio stream

**Trigger:** `main()` before FreeRTOS starts.

**Where:** `Core/Src/main.c` calls `MX_USART1_UART_Init()`, `MX_SAI1_Init()` and `MX_SAI2_Init()`. Definitions in `Core/Src/usart.c` and `Core/Src/sai.c`.

**Why:** USART1 must be ready before module status bytes can be received. SAI settings define 48 kHz digital audio timing, 24-bit ADC word format and shared clocks. **SAI configuration alone is not proof that DMA has started**; `StartAudioTask()` later controls the DMA topology.

**Boot software:** `Core/Src/demo_app.c::Demo_AppInit()` calls `ConnectivityService_Init()` and initializes PCM modes, telecoil filters, 24-bit recovery, an optional notch and idle-squelch state. These are initialized **once before input data is processed**. The normal UI default is **MIC**; BT/TC receivers become active when requested.

## R1. BtTask sends module configuration and receives status

**Trigger:** native scheduler creates `StartBtTask()`; then the task loops continuously (including status polling).

**Files:** `Core/Src/demo_app.c::StartBtTask()`, `::poll_bt_uart()`, `Core/Src/bt_profile.c::BtProfile_Apply()`, and `Core/Src/qr_bt_volume.c::QrBtVolume_ConnectedEdge()`.

```c
/* R26.10 source excerpt: event at approximately 2 seconds after boot. */
if (!bluetooth_auto_discoverable_sent &&
    (uint32_t)(HAL_GetTick() - bluetooth_boot_ms) >= 2000U) {
    bluetooth_auto_discoverable_sent = 1U;
    if (s_bt_status_code < 2U) {
        g_demo_control.apply_bt_profile_request = 1U;
        s_bt_pair_request = 1U;
    }
}
```

BtTask applies the BT401 profile and requests discoverability after startup. Every roughly three seconds it may send `AT+TS` to query module status.

**Connection status RX is interrupt-driven**, not a blocking UART read in AudioTask:

```c
/* Abbreviated: HAL_UART_RxCpltCallback() for USART1 */
uint16_t head = g_bt_rx_head;
uint16_t next = (head + 1U) & (UART_RX_RING_SIZE - 1U);
if (next == g_bt_rx_tail) {
    g_demo_diag.bt_uart_overruns++;   // Control RX bytes lost
} else {
    g_bt_rx_ring[head] = g_bt_rx_irq_byte;
    g_bt_rx_head = next;
}
HAL_UART_Receive_IT(&huart1, &g_bt_rx_irq_byte, 1U);
```

**What happens next:** `poll_bt_uart()` drains the byte ring, builds CR/LF-terminated text lines and parses `TS+00` / `TS+01` / `TS+02`. In this implementation they become disconnected/waiting versus connected/playing status codes. A UART status is *different* from active audio in the PCM samples.

### Connection and reconnection volume edge

```c
/* Source excerpt: StartBtTask() */
const uint8_t linked = (uint8_t)(s_bt_status_code >= 2U);
if (QrBtVolume_ConnectedEdge(&volume_tracker, linked)) {
    if (bt_tx(QR_BT_CONNECT_VOLUME_COMMAND,
              sizeof(QR_BT_CONNECT_VOLUME_COMMAND) - 1U, NULL)) {
        (void)bt_tx(QR_BT_VOLUME_QUERY_COMMAND,
                    sizeof(QR_BT_VOLUME_QUERY_COMMAND) - 1U, NULL);
    }
}
```

`QrBtVolume_ConnectedEdge()` stores the **previous** link bit and returns true only on **disconnected → connected**. That re-issues the configured volume command on reconnection, rather than on every task iteration.

**Limit:** the project uses `AT+CA100` as requested, but published BT401 material describes a smaller documented CA range. **Successful UART transmit does not prove the module accepted that setting.** It does not directly set the phone's own media slider. Verify the exact module revision and received response.

**Integration with Anna:** touch E8 or a SOURCE BT command updates the requested source via ControlTask; it does **not** directly send raw PCM or start the BT UART task. BtTask and AudioTask coordinate using status/control state.

## R2. AudioTask selects the SAI receiver topology

**Trigger:** AudioTask starts or receives a mode/restart request from Anna.

**Where:** `Core/Src/demo_app.c::dma_desired_topology()`, `::stop_dma()`, `::start_dma()`.

```c
/* Source excerpt with irrelevant tests omitted */
if (!g_demo_control.audio_capture_enabled ||
    g_demo_control.test_tone_step != 0U) return 0U;
if (g_demo_control.requested_source_mode == DEMO_MODE_MIC_ONLY &&
    g_demo_control.direct_source == DEMO_DIRECT_OFF) return 0U;
if (g_demo_control.requested_source_mode == DEMO_MODE_BT_ONLY ||
    g_demo_control.direct_source == DEMO_DIRECT_BT) return 1U;
return 3U;
```

| Topology | Meaning | Why |
|---|---|---|
| **0** | No BT or TC SAI RX | MIC/OFF/test source; microphone DFSDM managed separately |
| **1** | SAI1A Bluetooth RX | BT only |
| **3** | SAI1A plus SAI2A telecoil RX | TC/AUTO; SAI1A is the audio clock master |

`start_dma()` zeros its input buffers, resets the speaker queue and calls Naomi's `SpeakerOutput_StartDma()` before requesting SAI1A/SAI2A receive DMA. `stop_dma()` stops the active receivers, stops speaker DMA and flushes stale buffers. This is why a legitimate source switch can briefly be silent while output re-primes.

## R3. Hardware finishes half a receive buffer

**Trigger:** DMA finishes a half/full transfer. Each half is 256 × `uint32_t` words = **128 stereo frames**, representing **2.667 ms** at 48 kHz.

**Where:** `Core/Src/demo_app.c::HAL_SAI_RxHalfCpltCallback()` and `::HAL_SAI_RxCpltCallback()`.

```c
/* R26.10 source excerpt */
if (hsai->Instance == SAI1_Block_A) {
    g_demo_diag.bt_half_callbacks++;
    if (g_demo_control.direct_source != DEMO_DIRECT_TC)
        capture_push_bt_from_isr(&g_bt_dma_words[0]);
} else if (hsai->Instance == SAI2_Block_A) {
    g_demo_diag.tc_half_callbacks++;
    capture_push_tc_from_isr(&g_tc_dma_words[0]);
}
```

The full-transfer callback passes `&g_bt_dma_words[DEMO_HALF_WORDS]` or `&g_tc_dma_words[DEMO_HALF_WORDS]`. This ensures the ISR copies the half that DMA has **finished**, not the half it is currently writing.

### Why use capture rings?

`g_bt_capture[8]` and `g_tc_capture[8]` are **custom eight-slot rings (seven usable)**. The producer ISR copies one immutable half-buffer snapshot, calls `__DMB()`, publishes the head index and wakes AudioTask with `vTaskNotifyGiveFromISR()`. The task consumer calls `capture_pop_bt()` or `capture_pop_tc()`.

```c
/* Representative R26.10 code: capture_push_tc_from_isr() */
uint8_t next = (head + 1U) & CAPTURE_RING_MASK;
if (next == g_tc_cap_tail) {
    g_demo_diag.tc_dma_overruns++;  // Never overwrite unread audio
    return;
}
memcpy(g_tc_capture[head].words, words,
       sizeof(g_tc_capture[head].words));
__DMB();
g_tc_cap_head = next;
vTaskNotifyGiveFromISR(g_audio_task, &higher);
```

**Failure:** `bt_dma_overruns` or `tc_dma_overruns` increasing means the task is not draining completed blocks fast enough. The rings are not FreeRTOS queues and do not appear under CubeMX Queues.

## R4. Decode one Bluetooth audio block

**Trigger:** AudioTask removes a BT capture block.

**Where:** `Core/Src/demo_app.c::process_bt_words()` → `Core/Src/pcm_transport.c::PcmTransport_DecodeStereoWordsMode()` → `process_bt_pcm()`.

```c
if (PcmTransport_DecodeStereoWordsMode(
      words, DEMO_FRAMES_PER_HALF, g_left, g_right,
      (PcmTransportDecodeMode_t)g_demo_control.pcm_decode_mode)
    != PCM_TRANSPORT_OK) return;
...
for (size_t i = 0U; i < DEMO_FRAMES_PER_HALF; ++i) {
    // Default: both stereo channels contribute to the mono bone conductor.
    g_mono[i] = Audio_MixStereoToMono(g_left[i], g_right[i]);
}
process_bt_pcm(g_mono, now);
```

The actual function has LEFT, RIGHT, MIX and AUTO channel modes; the excerpt shows the *normal MIX case*. The decoder interprets the 24-bit stereo ADC words and produces signed 16-bit L/R PCM. MIX avoids losing a phone recording present only in the right channel.

**Next:** `process_bt_pcm()` calculates RMS and peak-to-peak, updates `update_bt_presence()` hysteresis, advances the connectivity service, and calls `ConnectivityService_ProcessBluetoothBlock()`. It calls `process_selected_audio(g_routed, 2U)` only when Bluetooth is actually selected.

**Bluetooth connection ≠ Bluetooth audio:** `TS+` status can show connected while input RMS is near zero. The mode policy uses audio-presence qualification; diagnose UART and PCM independently.

## R5. Recover one weak telecoil block without throwing away the 24-bit signal

**Trigger:** AudioTask calls `process_tc_words()` after TC DMA capture.

**Where:** `Core/Src/demo_app.c::process_tc_words()` → `Core/Src/tc_s24_recovery.c::TcS24Recovery_Process()`.

```c
if (s_tc_pcm_decode_mode == PCM_DECODE_LOW24 &&
    s_tc24_level_enabled) {
    TcS24Recovery_SetCap(&s_tc24_level, s_tc24_level_cap_q16);
    if (!TcS24Recovery_Process(&s_tc24_level, words,
                              DEMO_FRAMES_PER_HALF,
                              g_left, g_right)) return;
} else {
    PcmTransport_DecodeStereoWordsMode(/* fallback PCM mode */);
}
```

`TcS24Recovery_Process()` sign-extends each ADC sample before reducing bit depth:

```c
/* Actual source implementation of one transport-word operation */
static int32_t sign_extend_s24(uint32_t word) {
    uint32_t v = word & UINT32_C(0x00FFFFFF);
    if (v & UINT32_C(0x00800000)) v |= UINT32_C(0xFF000000);
    return (int32_t)v;
}
```

It measures both channels' RMS/peaks in full precision, computes a bounded target RMS gain, caps the requested gain (4× configured), applies a peak guard, ramps between block gain values and emits signed 16-bit samples. A naïve right shift can crush weak samples (e.g. code 800 becomes ~3). **Gain also raises noise; this is not SNR improvement.**

**Next:** `process_tc_words()` measures left/right RMS and correlation. Negative L/R correlation can cause a channel to be selected rather than mixing signals that would cancel. An AUTO channel candidate must persist for about **96 blocks ≈ 256 ms** before switching. A louder channel is not necessarily a *cleaner* channel.

## R6. Manual telecoil and AUTO telecoil diverge

**Trigger:** `process_tc_pcm()` receives selected mono TC data.

**Where:** `Core/Src/demo_app.c::process_tc_pcm()`; `Core/Src/connectivity_service.c::ConnectivityService_ProcessTelecoilBlock()`.

```c
if (g_demo_control.requested_source_mode == DEMO_MODE_TC_ONLY &&
    g_demo_control.direct_source == DEMO_DIRECT_OFF) {
    g_demo_diag.active_source = 1U;
    if (g_r27_tc_clean_enabled) {
        TcCleanup_Block(&g_r27_tc_filter, mono, g_r27_tc_clean,
                        DEMO_FRAMES_PER_HALF);
        TcIdleSquelch_Process(&s_tc_idle, g_r27_tc_clean,
                              DEMO_FRAMES_PER_HALF);
        process_selected_audio(g_r27_tc_clean, 1U);
    } else process_selected_audio(mono, 1U);
} else if (g_demo_diag.active_source == 1U) {
    /* AUTO: the service's g_routed may already be separately filtered. */
    ...
}
```

**Manual TC:** deliberately plays actual SAI2A PCM even when AUTO signal qualification finds it too weak. When cleanup is enabled: recovered mono → `TcCleanup_Block()` → `TcIdleSquelch_Process()` → common DSP.

**AUTO TC:** `ConnectivityService_ProcessTelecoilBlock()` runs `TelecoilDetector_Process()`, updates source quality, applies source-state decisions and—if TC is selected—`TelecoilFilter_ProcessSample()` before the later cleanup stage. Thus **manual and AUTO can have different audible filter responses**. This is an actual architectural caveat, not a feature to conceal in documentation.

**Processing:** `TcCleanup_Block()` combines a gentle high-pass (~123 Hz), fixed fourth-order ~3.4 kHz low-pass and optional explicitly calibrated narrow notch. `TcIdleSquelch_Process()` learns an idle baseline and smoothly attenuates source-off/idle to around **−30 dB**, then opens for detected programme. It does **not** separate active speech from same-band interference.

**Failure:** a clean idle mode plus noisy programme often means the analogue telecoil SNR is poor, not that SAI2A DMA stopped. Measure raw source-off and source-on input, and compare pre/post DSP.

## R7. Connectivity is a state machine, not a blind switch

**Trigger:** periodic `step_service(now)` or arrival of a BT/TC PCM block.

**Where:** `Core/Src/connectivity_controller.c::Connectivity_Update()` and `Core/Src/connectivity_service.c::ConnectivityService_Step()` / source block handlers.

| Function | Input → output | Behavior |
|---|---|---|
| `ConnectivityService_Init()` | audio sample rate + time → service state | Initializes detection, gains, filters and controller |
| `ConnectivityService_ApplyCommand()` | enabled/disabled source command + time → status | Sets whether BT, TC or both are eligible |
| `ConnectivityService_Step()` | `ConnectivityLinkState_t` → service status | Applies timing, link activity and FSM decision |
| `ConnectivityService_ProcessBluetoothBlock()` | mono PCM + received/current timestamps → output PCM | Routes/attenuates BT only if active; rejects stale frames |
| `ConnectivityService_ProcessTelecoilBlock()` | mono PCM + timestamp → filtered output PCM | Updates telecoil detector; routes only when qualified and selected |
| `Connectivity_GetOutputs()` | controller state → output flags | Reports selected source/mute/mode |

A critical detail: source *connected* state reported by BT401 over UART is separate from the `bt_audio_present` bit generated from PCM activity. The routing controller can use actual audio presence to avoid selecting a connected-but-silent phone.

**Integration with Anna:** consumes requested UI mode. **Mariya:** passes signed mono blocks to shared DSP (or its MIC branch). **Naomi:** selected output eventually reaches Naomi's FIFO; connectivity logic never writes I²S samples directly.

## R8. Errors, experiments and how to demonstrate the path

| Symptom | First check | Where to inspect |
|---|---|---|
| BT401 shows connected but no sound | `TS+` responses then BT ADC RMS | `poll_bt_uart()`, `process_bt_pcm()` |
| No BT DMA counts | SAI1A clock and RX DMA | `start_dma()`, `HAL_SAI_RxHalfCpltCallback()` |
| No TC DMA counts | SAI1A clock master and SAI2A RX | `dma_desired_topology()`, `start_dma()` |
| Raw TC near zero | ADC alignment / front-end signal | `TC PROBE`, `process_tc_words()` |
| TC idle quiet but active noisy | Input SNR, recovery gain, filters | `TcS24Recovery_Process()`, `TcIdleSquelch_Process()` |
| Dropouts/clicks | RX overruns, TX underruns, source resets | capture rings and Naomi's `speaker_fill_region_isr()` |
| Reconnect volume not applied | Real `TS+00` → `TS+01/02` edge and BT401 response | `QrBtVolume_ConnectedEdge()`; verify actual module command support |

### Ryan's function finder — chronological

| Event | Implementation | Input → output / next subsystem |
|---|---|---|
| Boot UART/SAI | `Core/Src/usart.c`, `Core/Src/sai.c` | Peripheral initialization → BtTask/AudioTask |
| BT status bytes | `demo_app.c::HAL_UART_RxCpltCallback()` | USART1 byte → RX ring |
| BT text parsing | `demo_app.c::poll_bt_uart()` | RX ring → connection status |
| AT control | `demo_app.c::StartBtTask()` and `bt_profile.c` | profile/status requests → BT401 UART |
| Link edge | `qr_bt_volume.c::QrBtVolume_ConnectedEdge()` | prior/current status → reconnect flag |
| DMA topology | `demo_app.c::dma_desired_topology()`, `::start_dma()` | mode flags → SAI receivers |
| SAI interrupt | `demo_app.c::HAL_SAI_RxHalfCpltCallback()` | DMA half → capture ring |
| BT decode | `demo_app.c::process_bt_words()` / `pcm_transport.c` | stereo 24-bit → mono signed 16-bit |
| TC recovery | `demo_app.c::process_tc_words()` / `tc_s24_recovery.c` | stereo 24-bit → mono signed 16-bit |
| TC cleanup | `demo_app.c::process_tc_pcm()` / `tc_cleanup.c` / `tc_idle_squelch.c` | TC mono → conditioned TC mono |
| Source decision | `connectivity_service.c` / `connectivity_controller.c` | source metrics + policy → selected PCM |
| Playback handoff | `demo_app.c::process_selected_audio()` | selected PCM → shared DSP and Naomi FIFO |

### Viva trace — E9 selected then noisy TC programme

Explain Anna's E9 event → AudioTask starts mask 3 → RX half/full interrupts → TC capture ring → 24-bit recovery → channel selection → manual TC qualification bypass → cleanup and idle squelch → common DSP → Naomi output. **Then show the engineering limit:** no downstream digital gain makes a noisy analogue input inherently cleaner.

**Next:** [Anna](ANNA_CONTROLS.md) · [Mariya](MARIYA_AUDIO_DSP.md) · [Naomi](NAOMI_OUTPUT.md) · [All subsystems](README.md).
