# R30.4 — Real context-switch profiler

Your R30.3 hardware evidence proved TIM2 is advancing (`!PX dt=1910`), but FreeRTOS returned `kernel=0` and every `ulRunTimeCounter=0` despite six live tasks. R30.4 adds FreeRTOS `traceTASK_SWITCHED_IN` and `traceTASK_SWITCHED_OUT` hooks that independently accumulate TIM2 runtime **during actual task context switches**. This is not a synthetic test or CPU usage inferred from how fast a Python window updates.

`!PV` now emits its original `R29|PROOF|DATA` reply with appended `method`, `tsw`, and `tov` fields so there are no extra unexpected UART packets. **method=1** means kernel runtime accounting is working. **method=2** means actual context-switch instrumentation is used because the kernel counters returned zero. **tsw** is a real count of switch-outs. **tov** must remain zero; nonzero means the trace record table is insufficient.

## Reproduce and verify

1. Extract a fresh ZIP. Run `PREPARE_PROJECT.bat`. Require FINAL PREPARE PASS. Do not select CubeMX Generate Code afterwards.
2. Clean, Build and Flash under Debug configuration. Confirm UART `!HI` includes `build=R30_4_TRACE_ACCOUNTING`.
3. Wait 3 seconds. Send `!PX` to confirm the timer is running, then `!PV` twice, two seconds apart. Observe trace switch counter increasing. The first sample may warm up; the next should show `valid=1, reason=0`. In the same `!PV` packet, `method=2` is acceptable and accurately identifies the profiler used.
4. With audio STOPPED, send `!RX` and sample `!PV`; then send `!RS`, wait 4 seconds and sample `!PV` again. Background execution share should rise and IDLE should fall. Stop with `!RX`.
5. Start BT with `!BS`, play real audio, `!RS` again and sample `!PV` while audio plays. Confirm DMA activity with `!BC` and no new output underruns with `!DG`. Higher priority AudioTask can preempt the low priority stress task.
6. In STM32CubeIDE Debug → Expressions add `g_demo_debug`, pause, expand `profile_source`, `profile_trace_switches`, `valid`, `total_cpu_pct_x10` and task percentages. The built-in FreeRTOS Task List Run Time (%) column may continue to show N/A if vendor kernel counters remain zero; the trace data is independent evidence.

**Caveats:** CPU shares are based on elapsed scheduler task execution from a 1 MHz counter, with interrupts attributed to the interrupted task. Suspended debugging freezes all execution. The profiler cannot assign real percentages unless real task switches occur. A single-core Cortex-M4 performs concurrent, preemptively scheduled work rather than literally simultaneous task execution. A full ARM build and physical validation remain required.
