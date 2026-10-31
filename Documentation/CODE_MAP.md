# Code map — where is everything?

> **Source scope:** This map covers the older Nucleo-oriented main-branch application. The user-tested R17–R20 custom-PCB firmware exists as separately distributed CubeIDE packages and is not fully integrated here; see [current status](R20_HARDWARE_STATUS.md).

> **Actual compiled target:** `Core/`, not `References/` or `Recovery/`.  `References/` preserves group submissions; `Recovery/` is a restore mirror used by `PREPARE_PROJECT.bat`. Do not separately compile copies from those directories.

## Entry points & ownership

| I want to understand/change… | Start here | Critical entry point | Owner / risk |
| --- | --- | --- | --- |
| MCU setup and 48 MHz clock | [`Core/Src/main.c`](../Core/Src/main.c) | `main()` / `SystemClock_Config()` | MCU HAL; high risk |
| FreeRTOS startup | [`Core/Src/rtos_bootstrap.c`](../Core/Src/rtos_bootstrap.c) | `Demo_StartNativeRTOS()` | Do not add CMSIS scheduler |
| All task registration and priorities | [`Core/Src/demo_app.c`](../Core/Src/demo_app.c) | `create_child_tasks()` | High risk, profile after edits |
| Real audio capture and routing | [`Core/Src/demo_app.c`](../Core/Src/demo_app.c) | `StartAudioTask()`, `process_bt_words()`, `process_tc_words()` | AudioTask owns buffers |
| RTOS control and UART commands | [`Core/Src/demo_app.c`](../Core/Src/demo_app.c) | `Demo_ControlTask()`, `process_console_line()` | ControlTask; keep nonblocking |
| RTOS/CPU metrics | [`Core/Src/rtos_stats_timer.c`](../Core/Src/rtos_stats_timer.c) and [`Core/Src/demo_app.c`](../Core/Src/demo_app.c) | `r29_refresh_debug_snapshot()` | TIM2/TIM6 protected |
| Optional audio profiles, level observer | [`Core/Src/group_features.c`](../Core/Src/group_features.c) | `GroupFeatures_Process()` | AudioTask owns state; portable C |
| Overload fallback | [`Core/Src/group_guardian.c`](../Core/Src/group_guardian.c) | `GroupGuardian_Update()` | Only disables optional G1 DSP |
| **Nucleo B1 user interface** | [`Core/Src/group_ui.c`](../Core/Src/group_ui.c) | `GroupUi_Poll()` | ControlTask owns debounced UI; compileable host test |
| **DMA event handoff to AudioTask** | [`Core/Src/demo_app.c`](../Core/Src/demo_app.c) | `capture_push_bt_from_isr()`, `capture_push_tc_from_isr()`, `StartAudioTask()` | IRQ 5; direct-to-task notify; worst-case timing unverified |
| Rotary quadrature decoder | [`Core/Src/group_encoder.c`](../Core/Src/group_encoder.c) | `GroupEncoder_Update()` | Driver standalone; PCB port pending |
| Bluetooth vs telecoil source logic | [`Core/Src/connectivity_controller.c`](../Core/Src/connectivity_controller.c) | `Connectivity_Update()` | Pure C FSM; separate from power FSM |
| Source service integration | [`Core/Src/connectivity_service.c`](../Core/Src/connectivity_service.c) | `ConnectivityService_*` | Software adapter |
| Dynamics HPF/EQ/limiting | [`Core/Src/audio_dynamics.c`](../Core/Src/audio_dynamics.c) | `AudioDynamics_ProcessBlock()` | DSP golden-vector coverage |
| Output audio transfer | [`Core/Src/speaker_output.c`](../Core/Src/speaker_output.c) | speaker DMA callbacks | High risk; check output underruns |
| USB-C, power, analogue interfaces | [`Hardware/Original_KiCad`](../Hardware/Original_KiCad/) | KiCad project | Schematic != wired prototype |
| STM32 physical pad assignments | [`Hardware/STM32_U13_PINMAP.csv`](../Hardware/STM32_U13_PINMAP.csv) | U13 pins | Custom board differs from Nucleo |

## Debug commands and user interface

See [`Documentation/GROUP_FEATURE_MANUAL.md`](GROUP_FEATURE_MANUAL.md) for `!G…` profile controls. `!GP` reports profile/guardian telemetry. The R34 source also contains intentionally disruptive scheduling experiments—**never activate continuous-spin mode in a product showcase**.

## Source-navigation shortcuts

With VS Code or another editor use **Go to Symbol** and search `StartAudioTask`, `GroupFeatures_Process`, or `Connectivity_Update`. From the repo root:

```bash
rg -n 'GroupFeatures_Process|GroupGuardian_Update|Connectivity_Update' Core
rg -n 'xTaskCreate|xQueueSend|xQueueReceive|vTaskDelay' Core/Src/demo_app.c
rg -n '^void HAL_.*Callback' Core/Src/demo_app.c
```

If `rg` isn't installed, use `grep -R -n`. Only one task should mutate a module's state unless it is explicitly synchronized. FreeRTOS queues communicate **messages**, not ownership of arbitrary pointer lifetimes.

## Refactoring boundary

The historical [`demo_app.c`](../Core/Src/demo_app.c) is a large legacy integration module. Its size is recognised technical debt. A wholesale split risks SAI/DMA timing, RTOS tracing and CubeMX regeneration. Extract it incrementally **only with tests and an MCU build**, keeping interrupt ownership and externally visible symbols unchanged. This document intentionally does not claim the whole file has been refactored.
