# Individual viva: fast code navigation + rigorous answer framework

The rubric (pp. 4–6) allocates ~5 minutes to each of **system/interfaces**, **code**, **hypothetical troubleshooting**, and **reflection**. A complete answer must show whole-system understanding **and** your actual contribution. Bring the printed A3 diagram, GitHub source with commit SHA, KiCad schematic and real BOM. Do not memorise invented CPU percentages or hardware PASS claims.

## Show the real files immediately

| Lecturer asks | Actual source / proof | Explain |
|---|---|---|
| Who owns the audio peripherals? | `Core/Src/demo_app.c` → `StartAudioTask`, DMA callbacks; `Core/Src/sai.c`; `Core/Src/speaker_output.c` | DMA ISR snapshots completed PCM, then notifies AudioTask. AudioTask owns capture tails, source FSM, DSP parameters and output FIFO. HAL SAI2B DMA owns output transfer. |
| Who owns user input? | `Core/Src/group_ui.c` → `GroupUi_Poll`; `group_button_poll` inside `demo_app.c` | ControlTask polls PC13, debounces and queues event. Only AudioTask commits profile/source request. Nucleo-only hardware feature. |
| Show synchronization primitives and failure mode | `g_group_events` (`xQueueCreate`, `xQueueSend`, `xQueueReceive`); `vTaskNotifyGiveFromISR`, `ulTaskNotifyTake` | Queue carries structured commands; direct notify wakes high-priority audio worker after publishing DMA sample snapshot. Removing queue ownership risks simultaneous parameter mutation; omitting notification can delay audio until timeout. |
| Who manages source precedence / errors? | `Core/Src/connectivity_controller.c`, `connectivity_service.c`, `group_guardian.c` | Separate policy (no HAL dependencies), signal qualification, fallback, optional DSP degradation if capture errors grow. Not equivalent to actual radio-link status or hearing-protection certification. |
| Explain sample rate / deadlines | `DEMO_FRAMES_PER_HALF=128`, `48 kHz` target in C and SAI; SAI interrupts | 128 / 48000 = 2.667 ms of captured time per half-buffer. Actual deadline must account for buffering and DMA; measure worst-case processing, not average. |
| Explain hard-real-time safety | `Core/Src/group_guardian.c`, output limiter, source muting | Counter-triggered optional DSP bypass; limited smoothing and saturating arithmetic, but not verified maximum acoustic output. |
| Why use FreeRTOS? | `Core/Src/rtos_bootstrap.c`, `FreeRTOSConfig.h`, task creation in `demo_app.c` | Prioritized concurrency; audio wake-up via ISR direct notification; control and BT supervision independent. One Cortex-M4 core means one RUNNING task, others READY/BLOCKED. |
| How do you test performance? | `Validation/run_host_validation.sh`, `Validation/matlab/verify_audio_filters.m`, actual physical evidence register | Production C vectors vs independent filter equations (verification); separate calibrated external tests (system validation). |

## System / interfaces — answer by contract

For every interface, give **producer → data type & unit → transport mechanism → consumer → update rate → ownership → overflow / error policy → initialization ordering**.

Example: `SAI1A DMA RX` → 32-bit words containing PCM stereo → half/full interrupt → bounded eight-slot capture ring (seven usable) → AudioTask wakes on FreeRTOS direct notification → 128 frames per transfer at 48 kHz. ISR publishes a complete immutable snapshot before notifying. Overrun counts are real diagnostics. Owner of queue head in IRQ; owner of tail in AudioTask. This is a design description; test actual timing on STM32 to prove deadlines.

## Code — be ready to show the exact C lines

1. `create_child_tasks`: AudioTask priority 7, BtTask priority 5, ControlTask priority 6. **Operating build excludes 3 dummy stress tasks** via `dha_build_mode.h`; laboratory builds opt in explicitly.
2. `group_button_poll` and `GroupUi_Poll`: 5 ms poll, 45 ms debounce, single/double/triple/long press gesture timing and test coverage.
3. `group_audio_update`: only AudioTask consumes `GroupEvent` queue; optional DSP state owned in one context, no external synchronous mutation.
4. ISR callbacks: publish bounded DMA capture snapshot and notify AudioTask using FromISR API at FreeRTOS syscall-compatible IRQ priority.
5. `GroupGuardian_Update`: faults/near-full ring trigger degraded operation of **optional** group DSP, recover only after clean interval. Base output path is not disabled.
6. `ConnectivityController_Step`: actual source state policy, qualification, fault and recovery. M3 source idle does **not** prove MCU sleep.

## Troubleshooting — discriminate causes, not anecdotes

**Every few seconds an audio click:** First reproduce with fixed stimuli. Check actual underrun/overrun counters and DMA half/full cadence, then measure processing deadlines and buffer depth. If digital path is clean, evaluate analog amplifier rail/ripple and transducer coupling. Compare logging off/on; avoid serial prints in real-time task.

**Music distorts but sine is clean:** Test input crest factor, per-band clipping, signed PCM conversion and 24-to-16-bit alignment, aliasing of resampling, limiter response, and amplifier rail at peaks. Sweep levels while observing physical output; compare before/after DSP on bench.

**Queue full or missed notification:** Inspect producer vs consumer rates, queue occupancy over time, ISR priority >= syscall ceiling, buffer head publication preceding `vTaskNotifyGiveFromISR`, finite handling per scheduler activation; verify no notifications are sent before handle exists.

**UI freezes during audio:** Inspect priority and whether AudioTask stays continuously READY, blocked-time policy, task notification vs yield, and long blocking UART operations. Verify with actual runtime stats, not just debugger screenshot.

**USB resets on a different power supply:** Measure VBUS start-up, CC pull downs, supply droop, source selector, 3.3 V regulation, reset/BOOT0 and ground; do not infer charger compliance from USB-C connector.

## Reflection — credible examples

- *Technical trade-off*: one shared AudioTask reduces shared DSP-state races, but the CPU budget depends on keeping ISR work short and deferring low-priority operations.
- *Integration difficulty*: original group projects used different clocks, generated peripheral maps and RTOS wrappers. A single CubeIDE IOC must own peripheral generation, rather than copy several `main.c` files.
- *Next version*: resolve PCB pin/library/power defects, port DFSDM microphone, characterize acoustic SPL and full-board timing before additional TinyML; these are evidence-priority, not aesthetic polish.
- *Lessons*: link requirement → external test → raw data → engineer sign-off → same firmware SHA. A green CI badge is not proof of a functioning hearing aid.

## Contribution ledger (fill before viva)

| Group member | Files/functions personally written or integrated | Interfaces owned | Physical evidence | Cross-member dependency |
|---|---|---|---|---|
| *Fill from git history* | *Specify real functions, not blanket team ownership* | *Type/format/timing/error contract* | *Record test IDs* | *Name who you coordinated with* |

Do not assign authorship from an amalgamated ZIP filename. Each member should confirm ownership with commits or original records.
