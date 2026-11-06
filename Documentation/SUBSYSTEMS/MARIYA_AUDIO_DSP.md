# Mariya — Microphone acquisition and DSP: complete execution sequence

**Read in runtime order:** `main()` → DFSDM bring-up → MIC selection → PDM/DMA half-buffer → microphone queue → ambient DSP and features → shared DSP → Naomi's speaker queue.

[← Four subsystem index](README.md) · [Original 39-event timeline](../FIRMWARE_EXECUTION_GUIDE_R26.md) · [Interface contracts](INTERFACES.md)

> **Baseline:** QR261104.10 complete CubeIDE release ZIP, not a claim that GitHub's older source checkout already includes every new file. This walkthrough describes implemented functions and hardware settings; native CubeIDE and physical-board performance verification remain separate.

## What Mariya's subsystem owns

The **MP34DT05TR-A digital MEMS microphone** emits one-bit **PDM**, not an analogue voltage for the PCM1808 ADCs. The hardware signal uses **PC2 (STM32-generated PDM clock) and PB14 (PDM data)**. Its receiver is **DFSDM1 Channel 2, Filter 0, DMA1 Channel 4**. The output interface to the rest of the audio application is a **128-frame signed 16-bit mono PCM block**.

| Contract | Value in QR261104.10 |
|---|---|
| PDM clock | 48 MHz / 25 = **1.92 MHz** |
| DFSDM output rate | 1.92 MHz / 40 = nominal **48 kHz** |
| DFSDM filter | **Sinc3**, oversampling 40, continuous regular conversion |
| DMA | **DMA1 Channel 4**, circular, 32-bit peripheral and memory words |
| DMA buffer | 256 × `int32_t` (two halves, each 128 samples) |
| Capture ring | 8 allocated slots, **7 usable**, each 128 × `int16_t` |
| Producer → consumer | DFSDM half/full callback → `G1_MicTakeBlock()` in AudioTask |
| Error tracking | `mic_init_stage`, `mic_errors`, `mic_overflow`, `mic_blocks` |

```text
Board power-on
  └→ G1_BoardInit() → MX_DFSDM1_Init() → G1_MicReinit()
      ├→ DFSDM1 Channel 2 / Filter 0, PC2/PB14
      └→ DMA1 Channel 4, Sinc3/OSR 40
BootstrapTask → Demo_AppInit() → G1Ambient_Init()
AudioTask selects MIC → G1_MicStart()
  └→ DFSDM DMA half/full event
      └→ mic_push() [int32 → int16; ring write; task notification]
          └→ G1_MicTakeBlock() [ring read + DC blocker]
              └→ product_mic_condition() → G1Ambient_Process()
                  ├→ HPF / RMS / peak / zero crossings
                  ├→ AGC / limiter
                  └→ G1Scene_Observe() [unlabelled scene clusters]
                      └→ process_selected_audio(source=3)
                          └→ Naomi: output_queue_push() → SAI2B
```

## M0 — Power-on: make DFSDM easy to find

**Trigger:** `main()` calls `G1_BoardInit()` **before** FreeRTOS starts.

**Implementations:** `Core/Src/g1_product_board.c::G1_BoardInit()`, QR261104.10 `Core/Src/dfsdm.c::MX_DFSDM1_Init()` and `Core/Src/g1_product_board.c::G1_MicReinit()`. Public declarations: `Core/Inc/dfsdm.h` / `Core/Inc/g1_product_board.h`.

```c
/* R26.10: actual dfSDM.c entry point */
void MX_DFSDM1_Init(void)
{
    /* Board driver retains HAL handle ownership for safe retries. */
    (void)G1_MicReinit();
}
```

The named `dfsdm.c` file is a thin adapter **not a second DFSDM driver**. `G1_MicReinit()` is the single runtime owner of channel/filter/DMA initialization and restart. The actual ST HAL implementation is compiled in `Drivers/STM32L4xx_HAL_Driver/Src/stm32l4xx_hal_dfsdm.c`.

**Integration with Anna:** `G1_BoardInit()` attempts DFSDM before initializing MPR121 I²C. Therefore a stuck or missing touch controller does not have to stop microphone bring-up.

**CubeMX visibility versus runtime:** QR261104.10's `.ioc` describes DFSDM1, CH2/Filter0, pins and DMA for inspection. **The working application owns the HAL initialization and the DMA interrupt handler in C.** Regenerating CubeMX code without reconciling generated handlers/MSP can duplicate those definitions.

## M1 — Configure clocks, PDM filter, DMA and error states

**Trigger:** the boot adapter, or a bounded retry when MIC is requested but not ready.

**Function:** `G1_MicReinit()` in `Core/Src/g1_product_board.c`.

```c
/* Edited excerpt from the real initialization function. */
if (g_g1_board.mic_running) return HAL_BUSY;
++g_g1_board.mic_init_attempts;
...
if (hmic_filter.State != HAL_DFSDM_FILTER_STATE_RESET)
    (void)HAL_DFSDM_FilterDeInit(&hmic_filter);
if (hmic_channel.State != HAL_DFSDM_CHANNEL_STATE_RESET)
    (void)HAL_DFSDM_ChannelDeInit(&hmic_channel);
...
hmic_channel.Instance = DFSDM1_Channel2;
hmic_channel.Init.OutputClock.Activation = ENABLE;
hmic_channel.Init.OutputClock.Divider = 25U;
...
hmic_filter.Instance = DFSDM1_Filter0;
hmic_filter.Init.RegularParam.DmaMode = ENABLE;
hmic_filter.Init.FilterParam.SincOrder = DFSDM_FILTER_SINC3_ORDER;
hmic_filter.Init.FilterParam.Oversampling = 40U;
...
hdma_mic.Instance = DMA1_Channel4;
hdma_mic.Init.Request = DMA_REQUEST_0;
hdma_mic.Init.Direction = DMA_PERIPH_TO_MEMORY;
hdma_mic.Init.Mode = DMA_CIRCULAR;
hdma_mic.Init.PeriphDataAlignment = DMA_PDATAALIGN_WORD;
hdma_mic.Init.MemDataAlignment = DMA_MDATAALIGN_WORD;
__HAL_LINKDMA(&hmic_filter, hdmaReg, hdma_mic);
```

**How:** the clock on PC2 drives the microphone's PDM bitstream; PB14 receives the one-bit data. DFSDM's Sinc3 filter/decimator creates 48 kHz PCM in the STM32. DMA moves wide converted samples into RAM without requiring the CPU to service each sample.

**Why deinitialize first:** a failed prior HAL channel registration can survive partial initialization; retrying the same handles without releasing filter then channel may reproduce a software-state failure.

**Return/error contract:** `HAL_OK` means the channel, filter and DMA were initialized; `mic_ready` is set and stage 5 recorded. `HAL_BUSY` protects a currently running capture. Failure updates a stage/bitmask and `mic_last_hal_status`; it is not evidence of physical microphone signal.

## M2 — MIC selection arms capture

**Trigger:** Anna selects E11/MIC or normal boot UI defaults to MIC. AudioTask observes `requested_source_mode==DEMO_MODE_MIC_ONLY`.

**Function:** `StartAudioTask()` in `Core/Src/demo_app.c` calls `G1_MicStart()` in `g1_product_board.c`.

```c
/* Source excerpt: defer retries by 1000 ms. */
if (g_demo_control.requested_source_mode == DEMO_MODE_MIC_ONLY &&
    g_demo_control.audio_capture_enabled) {
    if (!g_g1_board.mic_running &&
        (int32_t)(now - s_mic_next_start_ms) >= 0) {
        if (!g_g1_board.mic_ready)
            if (G1_MicReinit() != HAL_OK) Demo_SetError(0xA301U);
        if (g_g1_board.mic_ready && G1_MicStart() != HAL_OK)
            Demo_SetError(0xA302U);
        s_mic_next_start_ms = now + 1000U;
    }
} else if (g_g1_board.mic_running) {
    (void)G1_MicStop();
}
```

`G1_MicStart()` resets MIC ring head/tail and calls `HAL_DFSDM_FilterRegularStart_DMA(&hmic_filter, s_pdm_dma, G1_MIC_FRAMES * 2U)`. The DFSDM DMA now runs continuously.

**Important distinction:** In MIC mode, Ryan's SAI receiver topology mask is **0**—but DFSDM capture is **independent and active**. Source exit invokes `G1_MicStop()`, not an unrelated SAI RX stop.

## M3 — A DFSDM DMA half-buffer completes

**Trigger:** DMA1 Channel 4 reaches half/full of the 256-word circular input buffer.

**Callbacks and producer:** `DMA1_Channel4_IRQHandler()`, `HAL_DFSDM_FilterRegConvHalfCpltCallback()`, `HAL_DFSDM_FilterRegConvCpltCallback()`, `mic_push()` in `Core/Src/g1_product_board.c`.

```c
/* Abbreviated actual source */
void HAL_DFSDM_FilterRegConvHalfCpltCallback(DFSDM_Filter_HandleTypeDef *h)
{
    if (h->Instance == DFSDM1_Filter0) {
        ++g_g1_board.mic_half;
        mic_push(s_pdm_dma);
    }
}
/* Full callback uses mic_push(s_pdm_dma + G1_MIC_FRAMES). */
static void mic_push(const int32_t *raw)
{
    uint8_t next = (s_head + 1U) & (G1_MIC_DEPTH - 1U);
    if (next == s_tail) { ++g_g1_board.mic_overflow; return; }
    for (unsigned i = 0; i < G1_MIC_FRAMES; ++i) {
        int32_t v = raw[i] >> 10;
        if (v > 32767) v = 32767;
        if (v < -32768) v = -32768;
        s_mic_queue[s_head][i] = (int16_t)v;
    }
    __DMB();
    s_head = next;
    g_g1_board.mic_blocks++;
    Demo_NotifyMicReadyFromISR();
}
```

**What crosses the boundary:** one immutable block of **128 signed 16-bit mono samples**. The ISR performs bounded conversion/queue insertion, but **not ambient AGC**. The ring allows AudioTask to be briefly delayed while acquisition continues.

**Failure:** if the next index equals the consumer tail, the ring is full, so the ISR increments `mic_overflow` and **drops the incoming block**. DMA cannot be back-pressured by ordinary software queue fullness.

## M4 — AudioTask removes a block, then DC-blocks it

**Trigger:** `G1_MicTakeBlock(g_mono)` finds a queued MIC slot.

**Location:** `Core/Src/g1_product_board.c::G1_MicTakeBlock()`. AudioTask drains up to six pending blocks per pass.

```c
if (dest == NULL || s_tail == s_head) return 0U;
memcpy(dest, s_mic_queue[s_tail], sizeof(s_mic_queue[0]));
for (unsigned i = 0; i < G1_MIC_FRAMES; ++i) {
    int32_t input = dest[i];
    s_mic_hp_state = ((s_mic_hp_state * 32604L) >> 15)
                     + input - s_mic_hp_prev_input;
    s_mic_hp_prev_input = (int16_t)input;
    /* Clamp result to signed 16-bit PCM. */
    ...
}
__DMB();
s_tail = (s_tail + 1U) & (G1_MIC_DEPTH - 1U);
return 1U;
```

The DC blocker runs in task context, not in the time-critical DFSDM interrupt. This protects downstream AGC headroom from offset.

**Next:** `product_mic_condition(g_mono)` → `G1Ambient_Process()`.

## M5 — Ambient high-pass, RMS/peak, AGC and feature extraction

**Trigger:** each MIC block retrieved by AudioTask.

**Files:** `Core/Src/demo_app.c::product_mic_condition()`; `Core/Src/g1_ambient_dsp.c::G1Ambient_Process()`.

```c
/* Core AGC gain law, excerpt. Unity is Q12=4096. */
const uint32_t rms = integer_sqrt((uint32_t)(energy / n));
st->input_rms = rms;
st->input_peak = peak;
st->zero_crossings = zc;
uint32_t desired = G1_AMBIENT_GAIN_ONE_Q12;
if (rms > 0U && peak > 0U) {
    uint64_t proposed =
        (uint64_t)G1_AMBIENT_TARGET_RMS * 4096U / rms;
    if (proposed > G1_AMBIENT_GAIN_MAX_Q12)
        proposed = G1_AMBIENT_GAIN_MAX_Q12;
    desired = (uint32_t)proposed;
    /* Also bound desired gain by current block peak. */
}
```

The processing order is: roughly **60 Hz DC/sub-bass rejection** → RMS/peak/zero-crossing metrics → simple scene features → bounded AGC → **~26,000-count peak ceiling**. Target RMS is **5,500**. The gain cap is **64×**, but amplified input noise remains noise.

The AGC reduces gain faster when a loud signal arrives and increases more gradually for weak material. The estimated ambient noise floor is **telemetry**, not a hard mute gate, so faint environmental sounds remain audible.

`product_mic_condition()` saves input metrics and supplies **pre-amplification** context bins for the preference learner; it does not mistake AGC-amplified quiet audio for naturally loud input.

### M5a — What does the scene learner actually do?

**File:** `Core/Src/g1_scene_ml.c::G1Scene_Observe()`.

It uses features describing compressed energy, zero crossings and crest ratio, chooses the nearest of **three online centroids**, and periodically updates a bounded center. These are **unlabelled acoustic clusters**, not a pretrained model that reliably recognizes speech, traffic or named sounds. Its confidence reflects relative feature-space separation, not validated classification accuracy.

### M5b — How does user adaptive preference differ?

**File:** `Core/Src/adaptive_preferences.c::G1Pref_Observe()` / `G1Pref_Feedback()`. It keeps **3 amplitude × 3 spectral bins**. Passive observation increments counts; only a deliberate volume-up/down action from Anna contributes a training feedback event. Preference adjustments are bounded and remain below the configured output ceiling.

**Integration:** Anna controls the feedback action; Mariya's features describe input context; Naomi applies the resulting bounded gain during output.

## M6 — Shared DSP: all input sources converge

**Trigger:** MIC, BT or TC passes a selected PCM block to `process_selected_audio(pre, source)`.

**Files:** `Core/Src/demo_app.c`, `Core/Src/audio_dynamics.c`, `Core/Src/group_features.c`, `Core/Src/dha_effects.c`.

```c
/* Source excerpt: shared processing and output handoff. */
if (source != g_last_output_source) {
    output_queue_reset();
    (void)AudioDynamics_Reset(&g_dynamics);
    g_last_output_source = source;
}
if (g_demo_control.dsp_enabled) {
    (void)AudioDynamics_ProcessBlock(&g_dynamics, pre,
        g_demo_processed_mono, DEMO_FRAMES_PER_HALF,
        g_demo_diag.muted != 0U);
} else if (g_demo_diag.muted) {
    memset(g_demo_processed_mono, 0, sizeof(g_demo_processed_mono));
} else {
    memcpy(g_demo_processed_mono, pre, sizeof(g_demo_processed_mono));
}
if (!g_group_guard_bypass)
    GroupFeatures_Process(&g_group_features, g_demo_processed_mono,
        g_demo_processed_mono, DEMO_FRAMES_PER_HALF);
DhaEffects_Process(g_demo_processed_mono, DEMO_FRAMES_PER_HALF,
                   (uint8_t)source);
output_queue_push(g_demo_processed_mono);
```

**Why the processing layers differ:**
- **Source-specific front end:** Ryan's BT decoding/TC recovery and Mariya's microphone AGC depend on physical signal type.
- **Common DSP:** all chosen audio blocks are 128 × signed 16-bit **mono** PCM, so the same optional dynamics/profile/effect processing can be reused.
- **Output:** Naomi receives mono PCM without needing to know whether it originated in PDM, induction-loop ADC or Bluetooth analogue output.

`AudioDynamics_ProcessBlock()` (150 Hz HPF, optional 1.6 kHz resonance EQ, compressor and limiter) is **OFF by default**; do not claim it always runs. `GroupFeatures_Process()` supports NEUTRAL/VOICE/MUSIC/WIND profiles and has a true bypass/identity path. `DhaEffects_Process()` can apply a room/reflection effect to Bluetooth and optional rumble control to MIC, not a room echo to TC.

A real **FreeRTOS queue**, `groupEvents`, holds eight structured two-byte control events. `group_audio_update()` consumes them in **AudioTask** so the group DSP state has one writer. This is different from the microphone/audio capture rings, which are custom C arrays.

## M7 — Source change, failure paths and verification

| Symptom/event | Evidence to inspect | Meaning |
|---|---|---|
| MIC unavailable at boot | `mic_init_attempts`, `mic_init_stage`, `mic_ready` | HAL initialization failed or did not run |
| MIC selected but no blocks | `mic_half`, `mic_full`, `mic_blocks` | DFSDM/DMA clocking or interrupt problem |
| MIC input queue full | `mic_overflow` | AudioTask too slow or blocked |
| PCM near zero | `mic_peak`, raw pre-DSP scope | PDM wiring/clock, scaling, weak actual signal |
| Grainy output at high gain | `gain_q12`, `limited_samples` and pre/post data | AGC/headroom or analogue input SNR |
| No output despite MIC blocks | `active_source`, `output_enabled`, Naomi FIFO stats | Routing/mute/output stage rather than DFSDM |
| Source toggled while MIC active | `G1_MicStop()`, `StartAudioTask()` mode branch | Ensures old conversion is not replayed |

**Source code can prove the configured rates, algorithms and counters, not measured input noise, acoustic gain or successful physical microphone playback.** For a viva, show `MIC STATUS` and scope samples on the actual board.

## Mariya's function locator, in original execution order

| Order | Function | Exact implementation location | Handoff |
|---|---|---|---|
| Boot | `MX_DFSDM1_Init()` | R26.10 `Core/Src/dfsdm.c` | Calls board-owned MIC initialization |
| Init/retry | `G1_MicReinit()` | `Core/Src/g1_product_board.c` | DFSDM CH2/Filter0 + DMA |
| Source start | `G1_MicStart()` | `Core/Src/g1_product_board.c` | Arms continuous DMA |
| DMA event | `HAL_DFSDM_FilterRegConvHalfCpltCallback()` / full callback | `Core/Src/g1_product_board.c` | 32-bit capture → `mic_push()` |
| Ring producer | `mic_push()` | `Core/Src/g1_product_board.c` | 128 × 16-bit PCM + task notify |
| Ring consumer | `G1_MicTakeBlock()` | `Core/Src/g1_product_board.c` | Copies PCM, DC-blocks |
| Ambient DSP | `product_mic_condition()` | `Core/Src/demo_app.c` | Calls `G1Ambient_Process()` |
| AGC/features | `G1Ambient_Process()` | `Core/Src/g1_ambient_dsp.c` | 16-bit PCM + metrics |
| Unsupervised cluster | `G1Scene_Observe()` | `Core/Src/g1_scene_ml.c` | Scene state |
| User preference | `G1Pref_Feedback()` | `Core/Src/adaptive_preferences.c` | Context-dependent bounded gain |
| Common pipeline | `process_selected_audio()` | `Core/Src/demo_app.c` | Optional DSP, output queue |
| Source stop | `G1_MicStop()` | `Core/Src/g1_product_board.c` | Stops DFSDM DMA |

**Viva example:** tap E11 → Anna requests MIC → AudioTask runs `G1_MicStart()` → DFSDM half-complete → `mic_push()` → `G1_MicTakeBlock()` → `G1Ambient_Process()` → `process_selected_audio(source=3)` → Naomi's output FIFO. Explain why microphone capture is **not** part of the SAI1A/SAI2A input topology.

**Continue:** [Anna](ANNA_CONTROLS.md) · [Ryan](RYAN_CONNECTIVITY.md) · [Naomi](NAOMI_OUTPUT.md) · [Subsystem index](README.md).
