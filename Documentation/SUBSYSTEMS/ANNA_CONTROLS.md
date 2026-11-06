# Anna — User controls, gestures and system monitoring

**Read this as an execution trace:** power on → MPR121/encoder setup → ControlTask polls → hardware event → UI state → request published → AudioTask or Naomi's speaker render responds. This document covers **Anna's subsystem boundary**, not an assertion about authorship of every line in shared integration files.

[← All four subsystems](README.md) · [39-event complete execution trace](../FIRMWARE_EXECUTION_GUIDE_R26.md) · [Interface contracts](INTERFACES.md)

> **Firmware basis:** QR261104.10 CubeIDE ZIP. GitHub's current firmware files may lag this release. Paths below refer to the release's *active* `Core/Src` tree; C examples are source excerpts or marked abbreviated.

## Anna's role in the whole system

Anna owns **human input and user-facing control state**, not the ADC data or speaker DMA. The physical inputs are the MPR121 capacitive touch sensor on **I²C1 PB6/PB7**, and the rotary encoder on **PA0/PA1 GPIO EXTI**. The user interface publishes flags/requests which **AudioTask** applies at a safe point. The optional bench UART **USART2** is parsed by ControlTask; BT401 UART **USART1 belongs to Ryan's BtTask**.

```text
Power/reset
  └→ main() → G1_BoardInit()               [shared setup / physical input]
      ├→ PA0/PA1 encoder EXTI + quadrature accumulator
      └→ I²C1 setup → touch_init() for MPR121

BootstrapTask → Demo_AppInit() → G1Ui_Init() [software default MIC]
  └→ Demo_ControlTask() (priority 6)
      └→ product_ui_poll(now) (~2 ms control loop)
          └→ G1_BoardPoll(now, &event)       [touch polling ~15 ms]
              ├→ encoder detents ─→ G1Ui_Encoder()
              └→ touch rising edges ─→ G1Ui_Touch()
                  └→ product_ui_commit(changed, ...)
                      ├→ source request + restart flag ─→ Ryan's routing/AudioTask
                      ├→ gain/mute/effect flags ─→ Naomi's output renderer
                      ├→ optional preference feedback ─→ Mariya's context features
                      └→ DhaFeedback_Request() ─→ speaker DMA cue render
```

## A0. Boot: configure the physical input hardware

**Trigger:** `main()` calls `G1_BoardInit()` before starting FreeRTOS.

**Implementation:** `Core/Src/g1_product_board.c::G1_BoardInit()`. **Header:** `Core/Inc/g1_product_board.h`.

```c
/* Source excerpt: encoder and touch initialization, unrelated lines omitted. */
p.Pin = GPIO_PIN_0 | GPIO_PIN_1;
p.Mode = GPIO_MODE_IT_RISING_FALLING;
p.Pull = GPIO_PULLUP;
HAL_GPIO_Init(GPIOA, &p);

GroupEncoder_Init(&s_encoder, /* sampled A/B phase at boot */);
s_encoder.transitions_per_step = 4U;

HAL_NVIC_SetPriority(EXTI0_IRQn, 7U, 0U);
HAL_NVIC_SetPriority(EXTI1_IRQn, 7U, 0U);
HAL_NVIC_EnableIRQ(EXTI0_IRQn);
HAL_NVIC_EnableIRQ(EXTI1_IRQn);

MX_DFSDM1_Init();          // Mariya: MIC initialization attempted first.
...
p.Pin = GPIO_PIN_6 | GPIO_PIN_7;
p.Mode = GPIO_MODE_AF_OD;  // I²C needs open-drain pins.
p.Alternate = GPIO_AF4_I2C1;
HAL_GPIO_Init(GPIOB, &p);
hi2c1_product.Instance = I2C1;
hi2c1_product.Init.Timing = 0x10D19CE4U;
...
if (HAL_I2C_Init(&hi2c1_product) != HAL_OK) {
    g_g1_board.init_error_bits |= 1U;
    touch_fail(HAL_GetTick());
} else {
    HAL_Delay(100U);
    touch_init(HAL_GetTick());
}
```

**Why each part exists:** the encoder's A/B edges are time-sensitive and captured via EXTI instead of relying solely on a slow polling task. `GroupEncoder_Init()` establishes a valid initial quadrature phase; four transitions represent a full detent. **TIM2 is not the encoder interface**; it is used for runtime profiling. The touch sensor uses I²C1; 100 ms before the first MPR121 transaction permits startup settling.

**Mariya integration:** DFSDM is attempted **before** the touch I²C bus; a stuck MPR121 bus should not prevent the independent microphone from initializing.

**Failure path:** a failed I²C initialization sets `init_error_bits`, and the touch driver records a failed probe rather than claiming success. The encoder remains a separately wired input.

## A1. An encoder GPIO interrupt occurs

**Trigger:** PA0 or PA1 changes logic state.

**Locations:** `Core/Src/stm32l4xx_it.c::HAL_GPIO_EXTI_Callback()` and `Core/Src/g1_product_board.c::G1_EncoderEdgeIRQ()`.

**Mechanism:** the ISR reads both encoder lines and updates a Gray-code/quadrature decoder, accumulating valid detents in `s_encoder_pending`. The ISR does **not** call the audio DSP, change SAI clocks or print large diagnostic strings.

**What happens next:** on a later control poll, `G1_BoardPoll()` atomically reads and clears `s_encoder_pending` inside a short interrupt-masked region. It clamps the number of detents to the event type's range and hands the count to the UI layer.

**Why atomic read-and-clear matters:** without it, an encoder edge could arrive between reading the count and resetting it, silently losing the increment.

## A2. ControlTask polls the MPR121 and creates a touch event

**Trigger:** the running `Demo_ControlTask()` calls `product_ui_poll(now)` on each loop; `G1_BoardPoll()` enforces the ~15 ms touch interval.

**Locations:** `Core/Src/demo_app.c::Demo_ControlTask()`, `::product_ui_poll()`; `Core/Src/g1_product_board.c::G1_BoardPoll()`.

```c
/* Source excerpt: MPR121 read and debounce */
if ((uint32_t)(now - s_last_touch_ms) < 15U) return;
s_last_touch_ms = now;
uint8_t state[2];
if (!touch_read(0x00U, state, 2U)) {
    ++g_g1_board.touch_errors;
    s_touch_ready = 0U;
    g_g1_board.touch_present = 0U;
    s_next_probe_ms = now + 2000U;
    return;
}
uint16_t touched = (uint16_t)state[0] |
                   ((uint16_t)state[1] << 8);
...
for (unsigned k = 8U; k <= 11U; ++k) {
    // 2-step integrator rejects brief touch glitches.
    // On stable rising touch, publish a single event.touch_rising bit.
}
```

**Data format:** a 12-bit touch bitmap, one bit per electrode. E8–E11 are the four actual button electrodes. The MPR121 **proximity bit 12 is handled separately** for wake; it is not a fifth source button.

**Why debounce:** a short accidental brush should not cause an audio-source restart. The integrator requires sustained touch state and has a re-trigger interval; it does not block the real-time audio path.

**Failure:** if the MPR121 cannot be read, the driver increments `touch_errors` and schedules a later re-probe. `G1_TouchReprobe()` publishes a request; blocking I²C re-initialization remains with ControlTask, not USART ISR.

## A3. Convert physical events into logical UI changes

**Trigger:** `G1_BoardPoll()` returns detents or a touch-rising bitmap.

**Location:** `Core/Src/g1_user_controls.c`. **Header:** `Core/Inc/g1_user_controls.h`.

```c
/* Verbatim source excerpt: button map. */
uint32_t G1Ui_Touch(G1UiState *s, uint16_t e)
{
  if (!s) return G1_UI_INVALID;
  uint32_t flags = 0U;
  if (e & (1U << 8))  flags |= source(s, G1_UI_BT);
  if (e & (1U << 9))  flags |= source(s, G1_UI_TC);
  if (e & (1U << 10)) flags |= mute(s, (uint8_t)!s->muted);
  if (e & (1U << 11)) flags |= source(s, G1_UI_MIC);
  return flags;
}
```

| Physical event | Short action | Long/chord behavior (implemented in `product_ui_poll()`) |
|---|---|---|
| E8 | Select BT | ~1 s: toggle spatial/room effect |
| E9 | Select TC | ~1 s: toggle 1 kHz bench tone |
| E10 | Toggle mute | ~1 s: toggle chirp; short mute is deferred until release |
| E11 | Select MIC | ~1 s: toggle microphone noise/rumble option |
| E8 + E11 | Two simultaneous pads | ~1.5 s: logical sleep; proximity can wake |

**Important separation:** `G1Ui_Touch()` is a lightweight state-change function. Gesture duration is interpreted by `product_ui_poll()` (ControlTask), not by the ADC DMA ISR.

**Encoder function:**

```c
uint32_t G1Ui_Encoder(G1UiState *s, int detents)
{
  if (!s || detents == 0) return 0U;
  if (detents > 128) detents = 128;
  if (detents < -128) detents = -128;
  int32_t v = (int32_t)s->gain_q15 +
              (int32_t)detents * (int32_t)G1_UI_STEP_Q15;
  if (v < 0) v = 0;
  return gain(s, (uint32_t)v);
}
```

A clockwise or anticlockwise sequence modifies a **bounded fixed-point Q15 gain**; the output amplifier is not commanded directly. The UI's 100% corresponds to the configured local output ceiling (0.75× digital amplitude in this release), not a measured acoustic or vibration level.

## A4. Publish a safe source, gain or mute request

**Trigger:** `G1Ui_Touch()` or `G1Ui_Encoder()` returns a change mask.

**Location:** `Core/Src/demo_app.c::product_ui_commit()`.

```c
/* Source excerpt; omitted effect and diagnostic branches. */
if (changed & G1_UI_SOURCE_CHANGED) {
    g_demo_control.requested_source_mode = s_product_ui.source;
    s_tc_filter_reset_requested = 1U;
    g_r28_route_restart_pending = 1U;
    g_demo_control.audio_capture_enabled = 1U;
    g_demo_control.test_tone_step = 0U;
}
if (changed & G1_UI_GAIN_CHANGED) {
    s_user_gain_q15 = s_product_ui.gain_q15;
}
if (changed & (G1_UI_SOURCE_CHANGED | G1_UI_MUTE_CHANGED)) {
    g_demo_control.output_enabled =
        (uint32_t)(!s_product_ui.muted &&
                   s_product_ui.source != G1_UI_OFF);
}
```

**Why it is safe:** this publishes requests/flags, while `StartAudioTask()` remains the runtime owner of `stop_dma()`, `start_dma()`, source FSM state and telecoil filter history.

**The exact cross-team handoffs:**
- **Anna → Ryan:** `requested_source_mode` changes eligibility of BT/TC and which ADC DMA path AudioTask starts. Ryan's service/driver processes actual PCM, not the touch bitmap.
- **Anna → Mariya:** MIC selection causes AudioTask to start/retry the DFSDM input and process ambient PCM. Local user gain changes can be recorded by the optional preference learner.
- **Anna → Naomi:** `output_enabled`, `output_gain_q15` and mute state are observed at speaker processing/refill time; a feedback cue may be queued through `DhaFeedback_Request()`.
- **Anna → diagnostics:** `product_ui_report()` exposes source/mute/volume status when the diagnostic console is active.

**Why not call HAL SAI directly here?** If ControlTask stopped DMA midway through an AudioTask block or output refill, the two contexts could disagree over which buffer or filter state is valid.

## A5. A source request is actually executed

**Trigger:** AudioTask wakes on DMA notification or bounded timeout.

**Location:** `Core/Src/demo_app.c::StartAudioTask()`.

The task consumes `g_r28_route_restart_pending`, stops old capture if necessary, reinitializes routing state, chooses `dma_desired_topology()` and starts the appropriate streams. Ryan's input driver and Naomi's speaker output are configured there. Anna does **not** own either DMA stream.

**Example: E9 TC selection:** E9 → rising bit 9 → `G1Ui_Touch()` produces `G1_UI_SOURCE_CHANGED` → `product_ui_commit()` sets TC and restart → AudioTask applies topology **3** (SAI1A clock master + SAI2A telecoil RX) → telecoil DMA callbacks begin → TC programme → Naomi output.

**Example: E10 mute:** user touches/release E10 → `G1Ui_Touch()` toggles mute → `output_enabled=0` → Naomi's next TX half-refill supplies zeros (with a short permitted acknowledgement cue). A volume value of zero is **not** identical to source OFF.

## A6. Monitor activity, diagnostics and faults

**Trigger:** periodic ControlTask loop or an optional USART2 command.

**Location:** `Core/Src/demo_app.c::Demo_ControlTask()`, `::poll_console()`, `::process_console_line()`, `::product_ui_status()`.

The control loop polls inputs, tracks runtime/task rates, and optionally parses diagnostic messages when `G1_DIAGNOSTICS_UART_ENABLE` is enabled. USART2 uses its own **256-byte RX ring**, distinct from Ryan's BT401 USART1 RX ring. No audio PCM travels through either UART ring.

Useful observable values: `touch_reads`, `touch_errors`, `encoder_detents`, `requested_source_mode`, `output_gain_q15`, active source, audio DMA counts and speaker FIFO/underruns. These distinguish “E9 touch never registered” from “E9 registered but TC ADC never delivered data”.

## A7. Sleep and proximity wake events

**Trigger:** E8+E11 held for ~1.5 s; wake after valid proximity sequence.

**Location:** `Core/Src/demo_app.c::qr_sleep_enter()`, `::qr_sleep_wake()`, `::product_ui_poll()`; `Core/Src/qr_sleep_policy.c`.

Logical sleep sets an **ISR-visible audio-sleep fence**, disables normal output/capture requests and asks BtTask to change module state. The speaker TX callback fills silence if `s_audio_sleep_active`. The touch/I²C path remains polled so proximity can wake the device. Wake restores mode, requests a fresh route initialization and signals BtTask to re-enable its profile/discovery behavior.

**Not a hardware claim:** logical sleep is not equivalent to proof of lowest-possible battery sleep current.

## Anna's function finder

| When in flow | Function | File | Consumes → produces |
|---|---|---|---|
| Power-on | `G1_BoardInit()` | `Core/Src/g1_product_board.c` | GPIO/I²C initialization → input readiness |
| Edge interrupt | `G1_EncoderEdgeIRQ()` | `Core/Src/g1_product_board.c` | A/B GPIO state → detent accumulator |
| Every control loop | `Demo_ControlTask()` | `Core/Src/demo_app.c` | RTOS tick → polling/command work |
| Input poll | `G1_BoardPoll()` | `Core/Src/g1_product_board.c` | hardware input → `G1PhysicalInput` |
| UI interpretation | `G1Ui_Touch()` | `Core/Src/g1_user_controls.c` | rising bitmap → changed flags |
| UI interpretation | `G1Ui_Encoder()` | `Core/Src/g1_user_controls.c` | signed detents → bounded Q15 gain |
| UART control | `G1Ui_Command()` | `Core/Src/g1_user_controls.c` | text command → changed flags/invalid |
| Application handoff | `product_ui_commit()` | `Core/Src/demo_app.c` | changed flags → audio-safe requests |
| Poll/gestures | `product_ui_poll()` | `Core/Src/demo_app.c` | hardware events and hold durations → commands |
| Sleep/wake | `qr_sleep_enter()/qr_sleep_wake()` | `Core/Src/demo_app.c` | logical state → audio fence + BT transition |

### Viva demonstration: tap E9 and follow it to sound

1. Show `G1_BoardPoll()` and why I²C reads are spaced ~15 ms; identify electrode 9 in `touch_rising`.
2. Show `G1Ui_Touch()` maps bit 9 to `G1_UI_TC`.
3. Show `product_ui_commit()` publishes source and restart flags instead of calling HAL SAI.
4. Switch to [Ryan's routing/input guide](RYAN_CONNECTIVITY.md): AudioTask restarts SAI topology and processes physical telecoil frames.
5. Switch to [Naomi's output guide](NAOMI_OUTPUT.md): inspect output FIFO and DMA callbacks.
6. If no sound, distinguish touch failure (`touch_errors`) from RX failure (`tc_half_callbacks`), source selection failure (`active_source`), mute (`output_enabled`) and speaker underrun (`speaker_underruns`).

**Next:** [Ryan](RYAN_CONNECTIVITY.md) · [Mariya](MARIYA_AUDIO_DSP.md) · [Naomi](NAOMI_OUTPUT.md) · [All subsystems](README.md).
