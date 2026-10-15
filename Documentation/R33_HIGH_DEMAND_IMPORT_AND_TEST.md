# R33 — High-demand priority-7 CPU scheduling demo (exact setup)

## What changed, and what absolutely must remain unchanged

This is the R32 complete STM32CubeIDE project with **ONLY the optional `!RH` CPU-load experiment strengthened**. `HighLoad7` and `AudioTask` are **both priority 7**. R33 keeps the full R31 `!RS` and `!RD` priority-2 tests. This is one single-core STM32L476 Cortex-M4: tasks NEVER execute simultaneously, even if both show high *accumulated* CPU percentages. The correct claim is **preemptive concurrent execution/time-slicing**. One task is RUNNING at any suspended instant; others are READY/DELAYED.

`!RH` now runs for at most 20 seconds, with 8,000 μs of CPU-intensive arithmetic per workload window **measured by the already-working TIM2 wall timer**, then **blocks for 3 RTOS ticks**. The CPU may preempt HighLoad7 during its arithmetic; therefore 8 ms is the elapsed compute-window cap, not a guarantee of 8 ms execution. It is substantially more demanding than R32's 350 μs burst. The blocking window is mandatory so priority-6 UART/ControlTask and priority-5 BtTask can run. Use `!RX` to stop immediately, or allow automatic timeout. **Never remove the blocking operation.** This experimental mode may distort real audio under load: run the quiet demo first and monitor speaker underruns/overflows during audio tests.

**No changes:** `FreeRTOSConfig.h`, TIM2/TIM6, context-switch/tick profiler, CubeIDE `.cproject`, clock, SAI/DMA, STM32 startup, production controller, BT/TC audio DSP, existing task priorities, 37 baseline checks. **Only changed**: `Core/Src/demo_app.c` (and its Recovery copy), dashboard labels/buttons, `CHECK_PROJECT.py`, R33 document/release marker.

## A. Import into STM32CubeIDE — follow the order precisely

1. **Back up your WORKING R32 directory**, terminate the debug session, close the dashboard, serial COM connection and STM32CubeIDE.
2. Create a fresh folder, e.g. `C:\STM32\R33_HIGH_DEMAND`, and extract the R33 ZIP. Locate the **inner** `261013-Individual_Demonstration_Connectivity` folder containing `.project`, `.cproject`, `.ioc`, `Core`, `Recovery`, `Dashboard`, `PREPARE_PROJECT.bat`.
3. Open STM32CubeIDE, choose **File → Switch Workspace → Other** and choose an empty new workspace, e.g. `C:\STM32\R33_workspace` (the project name is identical to R32; a new workspace avoids conflicts).
4. Choose **File → Import → General → Existing Projects into Workspace**. Select the inner project folder and **uncheck** **Copy projects into workspace**. Finish.
5. **Only if CubeIDE requires it**, open `.ioc` and choose **Project → Generate Code** **NOW**, before running PREPARE; do not manually change any peripheral or RTOS settings. Otherwise skip generation.
6. **Close CubeIDE completely**. In the inner project folder, double-click `PREPARE_PROJECT.bat`. This restores custom app sources, configs and compiler settings after any CubeMX generation, and patches LOCAL FreeRTOS `tasks.c` for CubeIDE runtime percentages. Demand `PROJECT VALIDATION: 37 passed, 0 failed`, `R33 OPTIONAL HIGH-DEMAND ... PASS`, and **`FINAL PREPARE PASS`**. Do not proceed with failure.
7. Reopen CubeIDE and that fresh workspace. Right-click project → **Refresh**. Under **Build Configurations → Set Active** select **Debug**. Choose **Project → Clean**, then **Project → Build Project**; demand **0 errors**. The application uses `-g3`, and **only `tasks.c`** receives the Debug `-O0` profiler workaround. Do not force the whole application to `-O0`.
8. Run → **Debug Configurations** → STM32 C/C++ Application, select launch file and check the `.elf` path points to **R33 workspace/import folder's Debug build**. Flash, press **Resume (F8)**.
9. Run `Dashboard/START_DASHBOARD.bat`, select the correct ST-LINK COM port, 115200 baud 8N1, then `!HI`. Verify **`build=R33_HIGH_DEMAND_BURSTS`**. If not, STOP: wrong binary was flashed.
10. Never use CubeMX Generate Code *after* PREPARE unless you **re-run PREPARE then Clean/Build/Flash**. There is exactly **one native FreeRTOS scheduler**; don't add HighLoad7 using CubeMX CMSIS task creation.

## B. Required CubeIDE/CubeMX settings — VERIFY, do not change

| Item | Working value |
| --- | --- |
| Board / MCU | NUCLEO-L476RG / STM32L476RGT6 |
| SYSCLK | 48 MHz, HSE 16 MHz PLLM 4 / PLLN 24 / PLLR 2 |
| HAL timebase | TIM6 (SysTick is FreeRTOS timebase) |
| RTOS tick | 1000 Hz; `configUSE_PREEMPTION=1`; time slicing ON (default) |
| RTOS heap | heap_4, 32768 bytes; check task creation warnings |
| TIM2 CPU profiling | Already working 1 MHz: **DO NOT TOUCH** |
| FreeRTOS kernel Debug | `tasks.c -O0`, retained runtime variable; others optimized |
| RTOS startup | `Demo_StartNativeRTOS()`, no duplicate CMSIS start |
| Task priorities | AudioTask 7, HighLoad7 7, ControlTask 6, BtTask 5, Background 2, CPULoadB 2, IDLE 0 |
| Debugger | DWARF `-g3`, launched from current R33 ELF |
| Peripheral DMA/SAI/UART | Preserve existing working configuration |

## C. Exact tests

**Baseline**: With no load, run `!HI`, then `!PV` twice separated by 2 seconds. Verify `valid=1` and note AudioTask/HighLoad7 percentages (time since reset). In CubeIDE **Window → Show View → FreeRTOS → FreeRTOS Task List**, confirm HighLoad7 priority 7 but mostly DELAYED. It should consume almost no runtime before activation.

**Best controlled experiment — QUIET + HIGH DEMAND**:

1. Open Dashboard → **LIVE RTOS TASK MONITOR**; click **QUIET + HIGH DEMAND** (stops capture, sends `!RH`). Alternatively from serial send `AUDIO STOP`, followed by `!RH`.
2. Read ACK: `mode=PRIORITY7_HIGH_DEMAND|burst_us=8000|block_ticks=3|duration_ms=20000`.
3. Watch live **HighLoad7** runtime bar and **`h7work`** advance **while AudioTask is also receiving CPU time**. Compare against baseline; numerical CPU shares depend on real task readiness and interrupts and are **not guaranteed 50/50**.
4. While load is active, **Suspend** CubeIDE briefly. The FreeRTOS Task List should contain AudioTask and HighLoad7 priority 7 and nonzero accumulating runtime; **one RUNNING task only** (the other READY or DELAYED). Immediately **Resume** to continue the timed experiment. Debug suspension halts all tasks/timers; it cannot prove tasks run during suspension.
5. Send `!RX` or wait for automatic 20 s timeout. HighLoad7 runtime share should fall in fresh interval telemetry; work counter should stop.

**Optional experiment — BT + HIGH DEMAND**:

1. Confirm Bluetooth playback works first via `!BS`, `!BC`, `!DG` and the real speaker.
2. Click **BT + HIGH DEMAND** (starts BT then arms `!RH`). Watch live measurements, listen for audio stutter, check speaker underrun and input overflow counters.
3. If distortion or errors increase, press **STOP CPU STRESS / LOAD** (`!RX`). An audio failure under a priority-7 synthetic workload means *the demo has overloaded available CPU*; do **not** claim safe concurrent audio operation in that case. The unchanged **BT + DUAL LOAD** priority-2 test is the safer real-time demonstration.

**Complementary experiment — DUAL QUIET (NO AUDIO)**: This runs both priority-2 CPU-bound tasks via `!RD`, and is often the cleanest proof of equal-priority time slicing with *two dummy workers*. Keep it; it does not turn on HighLoad7.

## D. Commands

| Command | Function |
| --- | --- |
| `!RH` | R33 priority-7 intensive workload for 20 s; only active on request |
| `!RX` | Stops ALL stress modes early |
| `!RS` | Existing 60 s one-worker, priority-2 saturation |
| `!RD` | Existing 60 s two-worker, priority-2 saturation |
| `!PV` | ~500 ms MCU CPU windows including `h7cpu10` (tenths of %) and `h7work` |
| `!RM` | Task runtime counters and priorities, including `h7run`, `h7work` |
| `!DG` | Physical audio/DMA error counters |
| `!HI` | Firmware identity marker |

**If your FreeRTOS Task List percentages look diluted:** They are cumulative since reset or profiling start. The dashboard's two successive `!RM` readings and the `!PV` rolling sampling provide better within-experiment CPU share. **Don't restart the profiler or change `tasks.c` to make the figure look bigger**. If the CPU shares don't rise, first verify `!RH` ACK and `hi7=1` before changing code.

## E. Validation limitations

This release is source and host-compiler checked but **has not been ARM-compiled, flashed or executed on this user's STM32 by this environment**. The intensive mode's real-world impact on audio and serial responsiveness must be demonstrated on your hardware. The CPU profiler settings themselves were intentionally not changed.
