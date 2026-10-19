# Viva architecture and code navigation — 20% individual

Rubric pp. 4–6: ~5 minutes each for system/interfaces, code, troubleshooting and reflection; random question in each category. Print the system diagram at **A3 or larger**, have KiCad schematic/BOM and the **final GitHub commit** ready. Be able to identify personally authored code (do NOT attribute AI-generated or other member contributions falsely).

## Peripheral and task ownership

| Resource | Current code entry | Logical owner | Mechanism / contract | Physical gate |
|---|---|---|---|---|
| SAI1A RX / Bluetooth PCM | `Core/Src/demo_app.c` DMA callbacks | ISR produces; AudioTask consumes | 128-frame captured halves; bounded ring; notified on publication; avoid overwriting unread data | Nucleo verified in prior R34 work; group board differs |
| SAI2A RX / telecoil PCM | `Core/Src/demo_app.c` callbacks | ISR / AudioTask | Same ring/notification contract; record overrun counters | Physical inductive source |
| SAI2B speaker TX | `Core/Src/speaker_output.c` | AudioTask/SAI DMA | PCM format / stereo channel layout / underrun behavior | Amplifier + mounted transducer |
| User interface | `Core/Src/group_ui.c`, `Core/Src/demo_app.c` | ControlTask | Debounce/poll and bounded command queue; AudioTask owns DSP state | Nucleo B1 only |
| Audio profiles | `Core/Src/group_features.c` | AudioTask exclusively | Fixed-memory PCM block API; 16-bit saturation; manual/auto heuristic | Audio effect needs physical A/B |
| Source policy | `Core/Src/connectivity_controller.c` | Connectivity service / controller | Validity events → M1–M4 state; source priority; callbacks | M3 is source-idle not physical STOP |
| Guardian | `Core/Src/group_guardian.c` | AudioTask | Fault counters and queue high water → optional DSP bypass with recovery | Host-tested, hardware evidence pending |
| Bluetooth UART/command | `Core/Src/demo_app.c` | BT/Control tasks | No blocking UART from audio ISR; command error/timeouts | Module configuration |
| TIM2 profiler / TIM6 HAL tick | `Core/Src/rtos_stats_timer.c`; `stm32l4xx_hal_timebase_tim.c` | Timer driver, scheduler | Timer role and IRQ priorities; do not assign encoder TIM2 casually | Debug only; not black-box evidence |

**Critical interface contract questions:** (a) data type/bit order/rate/channel mapping, (b) initialisation/startup order, (c) buffer ownership and lifetime, (d) event/notification update rate, (e) deadline, (f) overload/drop strategy, (g) recovery/error semantics. Explain separately for signal path and control path.

## Synchronisation defence

- **Direct task notifications:** the DMA ISR signals AudioTask quickly, avoiding a wasteful ready-loop. Notify after publishing the completed capture block; read the real G5 implementation for exact ISR/task handle assumptions.
- **FreeRTOS queue:** ControlTask posts `GroupEvent` commands to AudioTask; producer does not edit AudioTask-owned DSP data. Explain full-queue policy.
- **Mutex:** discuss only if actually present and used; do not invent an I²C mutex. If absent, explain why a future shared bus needs one.
- **Semaphore/event group:** explain applicability, but if not used in this compiled firmware, explicitly say so; the lecturer will inspect the code.
- **Priority:** AudioTask 7, ControlTask 6, Bluetooth 5, background 2. Priority-7 stress demo can starve ControlTask; must not run in operating build.

## Troubleshooting frameworks (practice real traces)

**Audio clicks every few seconds:** reproduce at fixed sample rate with debugging disabled → compare capture/tx underrun counters in engineering build → distinguish DMA half callbacks, buffer overflow/format, task preemption, analog supply noise, output transducer coupling → one-factor change, instrument output, document evidence.

**UI becomes sluggish:** determine whether AudioTask fails to block or lower-priority ControlTask is starved; check time-slicing, xTaskNotifyWait use, queue-full policy, and shared resource contention.

**Bluetooth works solo but fails after merge:** check startup ordering, SAI clock/pin alternate functions, DMA channel/interrupt ownership, sample format/channel mapping, VTOR/vector priorities and buffer lifetime.

**Higher-volume distortion:** separate ADC/PCM numerical clipping, DSP saturation, amplifier supply headroom, transducer nonlinearity; measure levels at appropriate external points.

**Long latency:** decompose Bluetooth transport + ADC + DMA buffering + scheduler wake-up + DSP + speaker/output. The 2.667 ms /128-sample block is a throughput scale, not automatically end-to-end latency.

## Reflection prompts

Know the reason for each design trade-off (DMA vs polling, block size vs latency, gain headroom vs loudness, priority vs starvation, normal vs safe degradation). Explain one change you would make in PCB + enclosure co-design, one unresolved hardware defect, and a credible next revision plan. Describe exact contributions honestly, with `git log`, file/function mapping and interaction contracts.

Review all original question wording in *System Demonstration and Viva*, pp. 4–6; no generic script can predict the randomly selected questions.
