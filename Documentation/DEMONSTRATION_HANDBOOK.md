# R30 — Individual Demonstration Handbook

## Rubric: what the lecturer is actually grading

From the supplied **ZEIT4230 Individual Demonstration Instructions and Rubric** (included in `Documentation/Reference`):

- **Hardware and batteries (Y/N, with possible penalties)**: STM32, student's own electronics, correctly powered battery rails, reasonable board assembly.
- **60% functionality**: actual higher-level code from PD5b (DSP, state machines, etc.), without the testing harness, integrated with the low-level HAL and surrounded by FreeRTOS tasks. Demonstrate observed behaviour and timely operation with dummy work. A single firmware image; no recompiling during the session. Lecturer may vary the experiment. **Quantitative measurements are not mandatory.**
- **40% oral questions**: detailed explanation of own hardware/software, high-level understanding of others, and evidence-backed fault diagnosis. Don't depend on web or AI during the assessment.
- The project should contain **only the student's individual contribution** (or approved dependencies). Verify ownership of every high-level module before submission; the STM32Cube/HAL, middleware and third-party drivers are platform dependencies, not claims of original authorship.

## Exact 20-minute rehearsal

| Time | Live action | Evidence to show | Do not overclaim |
|---|---|---|---|
| 0–2 min | Connect COM, `!HI`, Stage 1 Health | 48 MHz HAL configuration, USART2 IRQ, deltas in four task-loop counters | HAL clock calculation ≠ independent measured oscillator |
| 2–4 min | Stage 2 Tone | Hear 1 kHz speaker; real SAI2B DMA callbacks | Callback count alone does not prove sound quality |
| 4–7 min | Stage 3 Bluetooth | Play genuine BT audio; SAI1 PCM RMS, capture blocks, speaker DMA and audible signal | Direct `!BS` does not traverse production FSM |
| 7–9 min | Stage 4 Telecoil | Real loop field, SAI2 PCM, noise filter ON/BYPASS, audible signal | Noisy TC can still have DMA PASS |
| 9–12 min | **Stage 5 RTOS** | Window shows task deltas and measured FreeRTOS/TIM2 runtime OFF vs STRESS, actual BT DMA and speaker TX | CPU tasks **share** one Cortex-M4, not multicore parallelism |
| 12–15 min | **QUIET CPU TEST** | Stop audio capture then stress: Background gets spare CPU, IDLE should approach zero. Save RTOS CSV | If AudioTask previously showed 93% CPU, Background cannot overtake it at priority 2 |
| 15–17 min | **BT + STRESS** | Real Bluetooth restarts and preempts Background; four counters progress; listen for interruptions; inspect under/overruns | A high AudioTask runtime is a limitation, not something to hide |
| 17–19 min | **LIVE PRODUCTION FSM** | ARM `!FA`, observe BT/TC inputs, M1–M4, selected source, switches, fault and DMA; save real trace | Recovered source may need hysteresis/recovery delay; no synthetic sequence |
| 19–20 min | Questions and files | Open exact source paths and the saved traces | Do not claim test harness runs on production firmware |

## Five numbered tests vs separate scientific tools

**Numbered hardware tests**: (1) Health/UART; (2) speaker output; (3) Bluetooth; (4) telecoil; (5) FreeRTOS load. Every test has real MCU response and (where appropriate) physical listening confirmation. Each saves a results ZIP without opening Explorer.

**Tools** (not scored by a synthetic PASS): `OPEN LIVE SCOPE / FFT` (real before/after PCM snapshots, time/FFT/waterfall), `LIVE PRODUCTION STATE MACHINE` (reads `!FV`, displays actual controller output), `SAVE REAL FSM TRACE`, `SAVE RTOS TRACE`, and optional file-based WAV studio (explicitly PC file, not STM32 measurement). The last was deliberately not a numbered test.

## How the state machine works and why it is replaceable

- **`Core/Src/connectivity_controller.c` / `Core/Inc/connectivity_controller.h`**: deterministic hardware-independent M1 INITIALISING, M2 ACTIVE, M3 IDLE, M4 FAULT. `Connectivity_Update()` consumes booleans (`interfaces_ready`, `paired_device_available`, `bluetooth_connected`, `telecoil_valid`) and monotonic millisecond timestamps. `best_valid_source()` gives Bluetooth priority when both sources are qualified. Pure C; no STM32 HAL, UART or RTOS dependency.
- **`Core/Src/connectivity_service.c` / `Core/Inc/connectivity_service.h`**: portable integration facade. `ConnectivityService_Step()` advances controller; `ConnectivityService_ProcessBluetoothBlock()` and `ConnectivityService_ProcessTelecoilBlock()` perform PCM routing, timing and signal validity handling; `ConnectivityService_ApplyCommand()` changes enabled sources. Changing the physical receiver/ADC should require a new hardware adapter, not rewriting the controller.
- **`Core/Src/demo_app.c`**: hardware adapter. `process_bt_words()` / `process_tc_words()` decode actual SAI DMA samples, `process_bt_pcm()` / `process_tc_pcm()` call `ConnectivityService`, `step_service()` feeds observed detector/link conditions, `sync_status_from_service()` maps controller outputs to physical output routing.
- **`!FA`** arms real AUTO (restarts the production service). **`!FV`** returns real observed mode, selected source, fault, BT/TC detector validity, DMA counters, speaker blocks, epoch and source-switch counter. `Dashboard/connectivity_dashboard.py` → `open_fsm_monitor()` displays those observations and exports a CSV. Neither forcibly selects a fake source.
- **Important limitation:** `bluetooth_connected` in this demonstration is inferred from **actual SAI1 audio/PCM presence** with threshold/calibration; it is not a verified Bluetooth radio connection-state event. Stop playback to test audio-presence loss, not necessarily RF disconnection.
- **IMPORTANT scope distinction:** PD2's full-device OFF/SLEEP system states are **not fully implemented as a product power-management FSM** in this connectivity subsystem. The M1–M4 names are connectivity-state definitions. Do not claim the full wearable UI, proximity, battery or microphone system is present.

### PD5 validation and changed detector timing

The **actual portable `connectivity_controller.c`** matches PD5 source byte-for-byte. Compiled host Unity tests: **52/52 pass**, covering states, priority, fault, recovery, timing and argument validation.

The current `telecoil_detector.c` intentionally changed `required_present_blocks` **2→24** and `required_absent_blocks` **3→75** to avoid chatter. Three original service tests still assume the old 2/3-block timing; they fail against this revised configuration. A **separate compiled regression for the current code** passed the new 24/75 thresholds, telecoil acquisition by `ConnectivityService`, and Bluetooth priority. No fake data or host harness is flashed onto STM32—the generated host stimuli are only offline unit-test inputs.

These results support portable logic; they do **not** prove actual real-world TC/BT state changes on the board. Export a live `!FV` trace with physical inputs for that evidence.

## Real FreeRTOS and CPU stress proof

- Startup: `Core/Src/main.c` calls `Demo_StartNativeRTOS()` in `Core/Src/rtos_bootstrap.c`. It creates **ControlTask**, then scheduler starts and creates AudioTask (priority 7), BtTask (5), Background (2). Control runs at priority 6. The `.ioc` includes the task metadata; **native `xTaskCreate` is authoritative** to avoid double-creation. The debugger FreeRTOS Task List shows actual task objects; it is not necessarily a live feed while running.
- **`Core/Src/demo_app.c` → `StartAudioTask()`**: handles SAI buffer snapshots and PD5 processing. Its `taskYIELD()` with pending queues does not give time to lower-priority Background. In prior observations it accounted for ~93% CPU. This must be acknowledged; do not change priorities just to make a graph look impressive.
- **`StartBackgroundTask()`**: `!RS` starts 60 seconds of real integer computation with a short periodic housekeeping delay and auto-off; `!RX` stops. It is deliberately *low priority*. The variable `g_stress_checksum` ensures computed work is not removed as dead code.
- `Core/Inc/FreeRTOSConfig.h` enables preemption, runtime trace, stack high water, overflow checking and 1 kHz ticks. `Core/Src/rtos_stats_timer.c` provides a 1 MHz TIM2 counter. `r28_rtos_snapshot()` calls kernel task-state/runtime APIs. `g_demo_debug` is an expression-watch snapshot refreshed about every 500 ms. `!RM`/`!PV` report actual board counters.
- GUI `LIVE RTOS TASK MONITOR`: loop deltas, sampled kernel state, per-task runtime percentage, minimum-free-stack, actual work blocks, BT/TC RX DMA, speaker TX DMA, underruns and queue overruns; `SAVE RTOS TRACE` writes timestamped board data CSV.

### Two defensible scheduling demonstrations

**A. Quiet saturation (clear near-100% illustration)**: Start `LIVE RTOS TASK MONITOR`, click **QUIET CPU TEST**. Firmware receives `AUDIO STOP` (stops SAI DMA) and `!RS` (runs low-priority arithmetic for 60 s). When higher-priority tasks are mostly delayed, Background should take the otherwise idle CPU. Confirm **Background runtime increases strongly; IDLE runtime falls**, Control/BtTask still make progress. `!RX` to stop. This does not test audio performance.

**B. Real audio under stress (integrated evidence)**: Start real Bluetooth playback using **BT + STRESS**. It sends `!BS`, *then* `!RS` because `!BS` resets the background load. Real DMA/PCM/speaker and the foreground tasks must continue. The lower-priority Background only gets leftover cycles. Check audio is audible and uninterrupted; save trace; compare underrun/overrun deltas. If AudioTask needs 93% CPU, Background may show only a few percent—this is **correct priority behaviour**, not failed concurrency. It is also a performance issue worth investigating if audio continuity is marginal.

**Scheduler is single-core**: at each instant one CPU task is running. FreeRTOS provides preemptive **concurrency**, not simultaneous multicore task execution. DMA engines independently move samples.

**Runtime percentages**: evaluate time-separated readings from `uxTaskGetSystemState` with TIM2, or view CubeIDE's FreeRTOS Task List (Suspend target to refresh; `Run Time (%)` is accumulated since scheduler start and may retain previous work history). A near-zero IDLE share means high total CPU utilization. Runtime counters may attribute interrupt time to an interrupted task. Do not fake CPU percentages or claim hard real-time guarantees from averages.

## Key code paths for questions

| Explanation | File / function |
|---|---|
| Actual system clock from HAL PLL register config | `Core/Src/main.c` → `SystemClock_Config`; `demo_app.c` → `refresh_clocks` |
| Health and UART error counters | `demo_app.c` → `r21_health`, `HAL_UART_RxCpltCallback`, `HAL_UART_ErrorCallback` |
| DMA half/full callbacks, buffer rings | `demo_app.c` → `HAL_SAI_RxHalfCpltCallback`, `HAL_SAI_RxCpltCallback`, `capture_push_bt_from_isr`, `capture_push_tc_from_isr` |
| 24-bit stereo input → signed 16-bit PCM | `Core/Src/pcm_transport.c` and `Core/Src/audio_processing.c` |
| Direct BT smoothed mono, AGC/output soft limiter | `demo_app.c` → `direct_process_block`, `scale_output_sample` |
| Telecoil cleanup | `Core/Src/tc_cleanup.c` → `TcCleanup_Block` |
| Production state-machine policy | `Core/Src/connectivity_controller.c` → `Connectivity_Update`, `best_valid_source` |
| Real service adapter | `Core/Src/connectivity_service.c`; `demo_app.c` → `step_service` / `sync_status_from_service` |
| RTOS tasks and CPU stress | `Core/Src/rtos_bootstrap.c`; `demo_app.c` → `create_child_tasks`, `StartAudioTask`, `StartBackgroundTask`, `r28_rtos_snapshot` |
| Source-independent RTOS profiling timer | `Core/Src/rtos_stats_timer.c`; `Core/Inc/FreeRTOSConfig.h` |
| Python GUI tests, monitors and CSV export | `Dashboard/connectivity_dashboard.py` → `worker`, `open_rtos_monitor`, `open_fsm_monitor`, `save_rtos_trace`, `save_fsm_trace` |

## Strict grade (provisional, not the lecturer's mark)

**Current proven evidence: 65/100 indicative.** Higher-level functionality **40/60**: strong actual BT/TC audio and portable FSM tests, FreeRTOS tasks/debugger percentages and genuine CPU load exist, but physical AUTO route transitions and successful integrated BT+stress continuity have not been demonstrated in the latest R30 firmware; full device PD2 hardware not present. Oral **25/40 assumed**: there is no observed oral exam; this is merely a placeholder.

**If** hardware stages, actual source changes, low-priority stress vs audio and uninterrupted output all work in one flashed image, functionality could reasonably move into the **48–53/60** range. The 40-mark oral component and any hardware/power penalties remain independently assessed.

**High-distinction risk factors**: inability to explain the difference between direct `!BS` and production `!FA`, no physical evidence for FSM mode/fault, AudioTask >90% CPU with unexplained scheduling, claiming a synthetic offline test is a live board test, or claiming team-mate firmware as individual work.
