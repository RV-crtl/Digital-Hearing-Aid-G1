# Naomi — Speaker output, DMA playback and continuity

**Follow the real sample:** boot initializes output requirements → AudioTask starts silent SAI2B DMA → selected PCM arrives from Ryan or Mariya → producer writes FIFO → SAI2B half/full IRQ consumes FIFO → volume/fade and mono-to-stereo → MAX98357 → bone-conduction transducer.

[← Four subsystem index](README.md) · [Original 39-event timeline](../FIRMWARE_EXECUTION_GUIDE_R26.md) · [Interface contracts](INTERFACES.md)

> **Baseline:** QR261104.10 release ZIP; GitHub firmware may still contain the older implementation. **There is no separate “speaker FreeRTOS task”.** The real-time output consumer is a DMA ISR, while AudioTask is the producer and controls initialization.

## Naomi's physical interface and exact contract

| Property | Implementation |
|---|---|
| Hardware chain | STM32L476 **SAI2 Block B** → **MAX98357** I²S input amplifier → bone-conduction transducer |
| SAI output pins | **PA15, PC10, PC12** (SAI2 alternate function 13) |
| Data format | **48 kHz, 16-bit, stereo-framed I²S**, normally identical mono samples in both slots |
| DMA | **DMA1 Channel 7**; memory-to-peripheral; circular, 16-bit halfword |
| DMA buffer | `g_speaker_tx_dma[DEMO_OUTPUT_DMA_SAMPLES]`; two halves, each 128 stereo frames |
| Source producer | AudioTask's `process_selected_audio()` → `output_queue_push()` |
| Source consumer | `HAL_SAI_TxHalfCpltCallback()` / full → `speaker_fill_region_isr()` |
| FIFO | **4,096 mono 16-bit samples** = up to ~85.3 ms stored at 48 kHz |
| Queue-full/empty | Producer drops a block on full; consumer conceals short underrun, then silence/re-prime |
| Priming | **1,024 mono samples** (~21.3 ms) normal clock-locked BT/TC; **2,048** (~42.7 ms) MIC |

**Ownership:** AudioTask writes to the FIFO; **SAI2B DMA interrupt** reads it. The FIFO is a custom single-producer/single-consumer ring, not a FreeRTOS queue. Anna publishes volume/mute; the output stage reads these settings. Ryan and Mariya never configure Naomi's SAI2B directly.

```text
main() → MX_SAI2_Init()              [general peripheral config]
BootstrapTask → Demo_AppInit()
AudioTask: start_dma()
    ├→ output_queue_reset()
    ├→ memset(g_speaker_tx_dma, 0)    [initially silence]
    └→ SpeakerOutput_StartDma()
        ├→ SpeakerOutput_Init()       [SAI2B + DMA1 Ch7]
        └→ HAL_SAI_Transmit_DMA()     [continuous output clock]

Ryan's BT/TC PCM ─┐
Mariya's MIC PCM ──┴→ process_selected_audio()
                        └→ output_queue_push()  [FIFO producer]
                            └→ DMA half/full complete [consumer event]
                                └→ speaker_fill_region_isr()
                                    ├→ prime when enough PCM exists
                                    ├→ exact / asynchronous FIFO reader
                                    ├→ scale_output_sample() + fade
                                    ├→ duplicate mono into L/R slots
                                    ├→ optional UI feedback cue
                                    └→ MAX98357 → bone conductor
```

## N0 — Boot: prepare the audio hardware

**Trigger:** `main()` initializes clocks and SAI peripheral descriptors. The actual speaker DMA begins **later** in AudioTask.

**Paths:** `Core/Src/main.c::main()`, `Core/Src/sai.c::MX_SAI2_Init()` and `Core/Src/speaker_output.c::SpeakerOutput_Init()`.

```c
/* Source excerpt: speaker_output.c */
hspeaker_sai2b.Instance = SAI2_Block_B;
hspeaker_sai2b.Init.AudioMode = SAI_MODEMASTER_TX;
hspeaker_sai2b.Init.AudioFrequency = SAI_AUDIO_FREQUENCY_48K;
hspeaker_sai2b.Init.MonoStereoMode = SAI_STEREOMODE;
...
st = HAL_SAI_InitProtocol(&hspeaker_sai2b,
                          SAI_I2S_STANDARD,
                          SAI_PROTOCOL_DATASIZE_16BIT, 2U);
```

`SpeakerOutput_Init()` configures the GPIO alternate functions, enables SAI2/DMA clocks, initializes an I²S master and sets DMA1 Ch7 as a circular **memory-to-peripheral halfword** transfer. It links `hdma_speaker_sai2b` to `hspeaker_sai2b` via `__HAL_LINKDMA()` and enables IRQ at priority 5.

**CubeMX accuracy warning:** DMA1 Ch7 and SAI2B are also described in the R26.10 `.ioc` for visibility, but the runtime implementation is **application-owned in speaker_output.c**. Do not generate a second `DMA1_Channel7_IRQHandler` in another source file without reconciling ownership.

**Integration with Ryan:** SAI2A is a different receiver block of SAI2; it is not the output transmitter. Input and output can run simultaneously with separate DMA streams.

## N1 — AudioTask starts output with a zero-filled buffer

**Trigger:** `StartAudioTask()` detects no running DMA or a changed requested topology.

**Location:** `Core/Src/demo_app.c::start_dma()`; `Core/Src/speaker_output.c::SpeakerOutput_StartDma()`.

```c
/* R26.10 source excerpt */
memset(g_speaker_tx_dma, 0, sizeof(g_speaker_tx_dma));
output_queue_reset();
...
HAL_StatusTypeDef c = SpeakerOutput_StartDma(
    g_speaker_tx_dma, DEMO_OUTPUT_DMA_SAMPLES);
if (c == HAL_OK) {
    g_speaker_dma_started = 1U;
    g_demo_diag.speaker_dma_running = 1U;
}
```

**Why zero first:** the amplifier should not transmit leftover RAM contents when the DMA first starts. Playback still waits for its FIFO priming threshold; the DMA can be clocking zeros before meaningful sound arrives.

**How DMA actually begins:**

```c
HAL_StatusTypeDef SpeakerOutput_StartDma(int16_t *samples,
                                         uint16_t sample_count)
{
    if (samples == NULL || sample_count == 0U) return HAL_ERROR;
    HAL_StatusTypeDef st = SpeakerOutput_Init();
    if (st != HAL_OK) return st;
    return HAL_SAI_Transmit_DMA(&hspeaker_sai2b,
                                (uint8_t *)samples, sample_count);
}
```

**Partial-input behavior:** the output can start even if one PCM1808 input fails. `start_dma()` reports receiver errors rather than automatically silencing a healthy speaker and independent input.

**Source change:** `stop_dma()` stops applicable DMA, calls `output_queue_reset()` and clears the TX buffer so a previous source cannot remain as stale audio. Short re-priming silence is intentional.

## N2 — Mariya or Ryan provides selected 16-bit mono PCM

**Trigger:** one decoded/processed 128-frame block reaches `process_selected_audio()`.

**Location:** `Core/Src/demo_app.c::process_selected_audio()` and `::output_queue_push()`.

**Contract:** `int16_t mono[128]` at nominal 48 kHz. The producer **does not write to Naomi's live DMA buffer**; it writes to the shared FIFO so output transmission is independent of the exact DSP-task wake-up instant.

```c
/* Source excerpt: output_queue_push() */
if (mono == NULL || !g_demo_control.output_enabled ||
    !g_demo_diag.speaker_dma_running ||
    g_demo_control.test_tone_step != 0U) return;
uint32_t w = g_output_write_count;
uint32_t r = g_output_read_count;
uint32_t used = w - r;
if ((OUTPUT_SAMPLE_FIFO_SAMPLES - used) < DEMO_FRAMES_PER_HALF) {
    g_demo_diag.speaker_queue_overruns++;
    return;
}
for (uint32_t i = 0; i < DEMO_FRAMES_PER_HALF; ++i)
    g_output_samples[(w + i) & OUTPUT_SAMPLE_FIFO_MASK] = mono[i];
__DMB();
g_output_write_count = w + DEMO_FRAMES_PER_HALF;
```

**How ring indexing works:** the read/write counts increase monotonically; indexing wraps using `& OUTPUT_SAMPLE_FIFO_MASK` because 4096 is a power of two. `__DMB()` publishes PCM values before the producer advances the write count.

**Why no general-purpose mutex:** the design has one task producer and one interrupt consumer. The source also uses a critical section in `output_queue_reset()` for multi-field read/write state changes. A FreeRTOS mutex cannot be acquired by the DMA interrupt.

**Overflow:** the producer refuses a block if fewer than 128 free sample slots remain; `speaker_queue_overruns` increments. This avoids overwriting unread samples.

## N3 — Hardware requests the next speaker half-buffer

**Trigger:** SAI2B TX DMA half-transfer / full-transfer interrupt. One half is **128 stereo frames ≈ 2.667 ms**.

**Location:** `Core/Src/speaker_output.c::DMA1_Channel7_IRQHandler()` → `Core/Src/demo_app.c::HAL_SAI_TxHalfCpltCallback()` or `HAL_SAI_TxCpltCallback()` → `speaker_fill_region_isr()`.

```c
void HAL_SAI_TxHalfCpltCallback(SAI_HandleTypeDef *hsai)
{
    if (hsai != NULL && hsai->Instance == SAI2_Block_B) {
        g_demo_diag.speaker_half_callbacks++;
        speaker_fill_region_isr(0U);
    }
}
void HAL_SAI_TxCpltCallback(SAI_HandleTypeDef *hsai)
{
    if (hsai != NULL && hsai->Instance == SAI2_Block_B) {
        g_demo_diag.speaker_full_callbacks++;
        speaker_fill_region_isr(DEMO_OUTPUT_HALF_SAMPLES);
    }
}
```

**Why refill inside the interrupt:** the half-buffer that just finished transmitting is safe to rewrite while the other half continues playing. Delaying the refill until a lower-priority FreeRTOS task was scheduled would create an unnecessary extra deadline dependency.

**What the IRQ may do:** access a prepared FIFO, apply bounded gain/fades, write 256 halfword samples into the safe DMA half and increment counters. It must not perform blocking UART transmission or memory allocation.

## N4 — Priming, exact clock-locked playback and microphone clock drift

**Trigger:** every `speaker_fill_region_isr()` call, if normal output is enabled.

```c
/* Source excerpt: decide when there is enough buffered audio. */
if (!g_output_playback_primed) {
    const uint32_t prime = output_clock_locked_path() ?
        OUTPUT_PRIME_SAMPLES_EXACT : OUTPUT_PRIME_SAMPLES_ASYNC;
    if (output_sample_depth() >= prime) {
        g_output_phase_q16 = 0U;
        g_output_step_q16 = OUTPUT_STEP_Q16_ONE;
        g_output_fade_q15 = 0U;
        g_output_playback_primed = 1U;
    }
}
if (g_output_playback_primed)
    have = output_queue_pop_isr(dst);
```

| Output path | How FIFO is consumed | Why |
|---|---|---|
| Ordinary BT / TC | `output_queue_pop_exact_isr()` | Same SAI clock domain: each prepared sample output once, no rate correction |
| MIC | `output_queue_pop_async_isr()` | DFSDM uses another clock domain; a very slow fractional FIFO reader compensates long-term drift |
| TC elastic diagnostic option | `output_queue_pop_async_isr()` when explicitly enabled | Research/debug fallback, **not normal TC playback** |

**Exact path code:**

```c
for (uint32_t i = 0U; i < DEMO_FRAMES_PER_HALF; ++i) {
    int16_t y = scale_output_sample(
        g_output_samples[(r + i) & OUTPUT_SAMPLE_FIFO_MASK]);
    y = apply_output_fade(y);
    stereo_dst[2U * i] = y;
    stereo_dst[2U * i + 1U] = y;
}
```

**Why duplicate?** The single bone-conduction programme is mono, but the MAX98357 expects serial audio framing with left and right channel slots. Writing both avoids losing samples due to left/right slot selection.

**Asynchronous path:** the reader smooths FIFO depth, adjusts a Q16 fractional read step gradually, and linearly interpolates between two source samples. It avoids large sudden packet drops to correct small long-term clock differences. It can add processing cost; therefore the normal BT/TC path deliberately stays exact.

## N5 — Local volume, mute, sleep and transient cues

**Trigger:** Anna changes local volume/mute, logical sleep is requested, or a short status cue is pending.

**Functions:** `scale_output_sample()`, `speaker_fill_region_isr()` in `Core/Src/demo_app.c`; `DhaFeedback_Request()` / `DhaFeedback_Render()` in `Core/Src/dha_feedback.c`.

```c
/* Source excerpt: bounded final local output gain */
int64_t y =
    ((int64_t)sample * (int64_t)g_demo_control.output_gain_q15) >> 15;
int64_t a = (y < 0) ? -y : y;
if (a > 26000LL) {
    a = 26000LL + ((a - 26000LL) >> 2);
    if (a > 31900LL) a = 31900LL;
    y = (y < 0) ? -a : a;
}
```

This is a cheap **soft knee** above ~26,000 PCM counts followed by a bound of approximately ±31,900. The local maximum user gain is `24576/32768 = 0.75×`. This reserves digital headroom; it is **not a calibrated safe acoustic limit**.

**Mute/sleep order (critical):**

```c
if (s_audio_sleep_active) {
    memset(dst, 0, DEMO_OUTPUT_HALF_SAMPLES * sizeof(int16_t));
    return;           // All audio and cues are suppressed during logical sleep.
}
if (!g_demo_control.output_enabled) {
    memset(dst, 0, DEMO_OUTPUT_HALF_SAMPLES * sizeof(int16_t));
    DhaFeedback_Render(dst, DEMO_FRAMES_PER_HALF);
    return;           // A short mute acknowledgement can still play.
}
```

**Cross-subsystem integration:** Anna's `product_ui_commit()` sets mode/gain/mute; Naomi observes them at the next output refill. A source-selection cue is requested via atomic bit operations, then `DhaFeedback_Render()` mixes the bounded short acknowledgement into stereo output. It is **not an extra microphone or Bluetooth source**.

**Test tone:** an explicit 1 kHz or chirp bench mode uses `QrDemoTest_Render()` directly in the speaker ISR, bypassing normal user PCM while retaining a bounded test level.

## N6 — Deadline miss, underrun and recovery

**Trigger:** speaker TX DMA needs data but the output FIFO doesn't contain enough samples.

**Where:** `Core/Src/demo_app.c::speaker_fill_region_isr()`.

```c
/* Source excerpt: short bounded concealment */
if (!have) {
    if (g_output_playback_primed &&
        g_r21_have_last_good_output &&
        g_r21_conceal_streak < 2U &&
        g_demo_diag.active_source != 0U &&
        g_demo_diag.muted == 0U) {
        /* Reuse previous valid output block with decaying amplitude. */
        g_r21_conceal_streak++;
        g_r21_conceal_total++;
        g_demo_diag.speaker_underruns++;
        return;
    }
    memset(dst, 0, DEMO_OUTPUT_HALF_SAMPLES * sizeof(int16_t));
    if (g_output_playback_primed && g_demo_diag.active_source != 0U) {
        g_demo_diag.speaker_underruns++;
        g_output_playback_primed = 0U;
        g_output_fade_q15 = 0U;
    }
}
```

**Behavior:** if a producer misses a short deadline, the ISR can replay a recent good output half (with decay) for **at most two halves**. If the deficit persists, it produces silence and waits for the FIFO to re-prime. A fade avoids a hard click on restart.

**This is concealment, not a fix for sustained overload.** An input queue overrun, excessive DSP execution time or source-change reset can still cause a discontinuity.

**Instrumentation:** `speaker_underruns`, `speaker_queue_overruns`, `speaker_refill_misses`, `speaker_half_callbacks` / `speaker_full_callbacks`, `speaker_queue_depth` and `speaker_blocks_output`. Match timestamped counters to the audible fault.

## N7 — Integration and error isolation

| Symptom | Most likely check order | Owner boundaries |
|---|---|---|
| No sound, but BT/TC/MIC input RMS exists | `output_enabled` → `speaker_dma_running` → FIFO depth → TX callbacks | Anna flags → Naomi driver |
| FIFO initially silent | `g_output_playback_primed` and priming threshold | Normal startup behavior |
| Repeating ~2.7 ms sound section | Concealment counters, FIFO read index | Naomi underrun path |
| Graininess only at high volume | pre/post DSP peaks, local gain, MAX98357 supply/transducer | Mariya/Naomi and hardware |
| UI says TC but old BT continues | `g_last_output_source` / `output_queue_reset()` | Anna/Ryan → Naomi handoff |
| Microphone sounds delayed | 2,048 sample prime + DFSDM collection + task timing | Mariya → Naomi clock boundary |
| No speaker callbacks | SAI2B pins, DMA1 Ch7, NVIC and `SpeakerOutput_StartDma()` | Naomi hardware path |

## Naomi's function finder, in run order

| Runtime event | Function | Implementation | Input → result |
|---|---|---|---|
| Peripheral setup | `MX_SAI2_Init()` | `Core/Src/sai.c` | SAI peripheral clocks/settings |
| Start requested | `start_dma()` | `Core/Src/demo_app.c` | Reset FIFO, zero DMA and start speaker |
| Driver initializes | `SpeakerOutput_Init()` | `Core/Src/speaker_output.c` | SAI2B + DMA1 Ch7 setup |
| DMA starts | `SpeakerOutput_StartDma()` | `Core/Src/speaker_output.c` | Circular I²S TX begins |
| PCM ready | `process_selected_audio()` | `Core/Src/demo_app.c` | Processed mono PCM |
| FIFO producer | `output_queue_push()` | `Core/Src/demo_app.c` | Adds 128 mono samples |
| DMA half/full IRQ | `HAL_SAI_TxHalfCpltCallback()` / full | `Core/Src/demo_app.c` | Calls refill on safe TX half |
| FIFO decision | `output_clock_locked_path()` | `Core/Src/demo_app.c` | Choose exact vs async reader |
| FIFO consumer | `output_queue_pop_exact_isr()` / `output_queue_pop_async_isr()` | `Core/Src/demo_app.c` | Gain/fade + two I²S slots |
| Speaker refill | `speaker_fill_region_isr()` | `Core/Src/demo_app.c` | Priming, mute, test, underrun concealment |
| UI cue | `DhaFeedback_Render()` | `Core/Src/dha_feedback.c` | Adds short status acknowledgement |
| Stop source | `stop_dma()` / `SpeakerOutput_StopDma()` | `demo_app.c` / `speaker_output.c` | Stop DMA, clear stale output |

**Viva example:** trace a half-buffer from Ryan's BT or Mariya's MIC → `process_selected_audio()` → `output_queue_push()` → SAI2B half-completion → `speaker_fill_region_isr()` → `scale_output_sample()` → duplicate mono to stereo → MAX98357. Then explain exactly what happens when the producer is late.

**Continue:** [Anna](ANNA_CONTROLS.md) · [Ryan](RYAN_CONNECTIVITY.md) · [Mariya](MARIYA_AUDIO_DSP.md) · [All subsystems](README.md).
