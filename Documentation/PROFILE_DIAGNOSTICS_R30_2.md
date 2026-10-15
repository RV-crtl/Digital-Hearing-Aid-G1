# R30.2 CPU runtime profiler — diagnostic release

`!PV` now reports a **real failure reason** instead of an unexplained `valid=0`. This does **not** fabricate usage percentages.

`reason`: 0=valid, 1=first sample/warming up, 2=FreeRTOS task snapshot unavailable, 3=TIM2 did not advance, 4=kernel per-task runtime counters did not advance, 5=sample window too short, 6=sample gap too large for safe 32-bit timer interpretation.

Additional fields: `timer` is TIM2 raw count; `kernel` is FreeRTOS total; `dt` is measured timer ticks since previous sample (normally ~500000); `kdt` is the FreeRTOS total difference; `wall` is HAL elapsed milliseconds; `runsum` is the sum of selected task runtime differences (does not include timer service and other tasks); `tasks` is the number returned by FreeRTOS. On a healthy sampler, `dt` and `kdt` should both advance, `runsum` should be nonzero, `reason=0`, and `valid=1`.

The previous implementation *incorrectly invalidated any observation with >2s between control task snapshots*, e.g., while servicing a slow HD UART transfer. R30.2 uses the actually elapsed TIM2 interval and rejects only genuinely unusable samples.

## How to test

1. Extract in a fresh folder. Run PREPARE_PROJECT.bat. Require FINAL PREPARE PASS, then import, Clean, Build, Flash (do NOT run Generate Code after PREPARE).
2. In terminal at 115200 baud enter `!HI`: require `build=R30_2_PROFILE_DIAG`.
3. Enter `!PV`, wait 2 seconds, and enter `!PV` again, then `!RM`. Inspect the `reason`, `dt`, `kdt`, `wall`, `runsum`, `tasks` fields.
4. With Bluetooth audio OFF, enter `!RX`, wait 2s, then `!PV`. Start `!RS` and wait 3 seconds; enter `!PV`. Verify background CPU percentage goes up while Idle goes down.
5. For Bluetooth under stress, start real BT `!BS`, then `!RS`. Monitor `!PV` and `!RM` and verify actual DMA and speaker counters advance. If audio occupies 93% CPU, Background's share will remain small; this is priority scheduling, not a failed stress loop.
6. In CubeIDE Debug FreeRTOS Task List, suspend to inspect the actual tasks. In Expressions, manually add `g_demo_debug` and inspect `profile_reason`, `profile_timer_delta`, `profile_kernel_delta`, `profile_accounted_delta`, and the percentage fields.

If `reason=3`, compare `TIM2->CR1` CEN bit and `TIM2->CNT` under debugger; if reason=4, check `configGENERATE_RUN_TIME_STATS=1`, proper current ELF and FreeRTOS `tasks.c` compiled at Debug -O0; if reason=2, check task snapshot size and FreeRTOS state.

## Bounds of verification

This project has been source-reviewed and host-tested here, but no ARM toolchain or physical STM32 is available for a full firmware link and runtime verification. Do not assert measured CPU values until you inspect the actual board output.
