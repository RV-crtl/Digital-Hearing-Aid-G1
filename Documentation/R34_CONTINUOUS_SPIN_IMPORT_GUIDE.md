# R34 — Continuous priority-7 busy loop and exact STM32CubeIDE setup

## What this release does

The original R33 `!RH` mode used an 8 ms computation window followed by a **3-tick blocking delay**; on the user's real STM32CubeIDE screenshot, `HighLoad7` consequently showed only about **5% cumulative runtime** against roughly 86–89% for `AudioTask`. R34 changes **only the optional `!RH` load mode** to a continuous high-priority busy loop with **no voluntary delay or yield during the 20-second active interval**. It remains priority **7**, equal to `AudioTask`.

This is a **deliberately disruptive educational experiment**. While the loop is active, a ready priority-7 dummy can prevent priority-6 `ControlTask` and priority-5 `BtTask` from executing. **UART dashboard queries and `!RX` can stall for up to 20 seconds**, and Bluetooth audio can glitch. The 20-second cutoff is checked **inside the busy loop using the existing TIM6 HAL tick**, so it does not depend on `ControlTask` processing a stop command. The board and debugger must remain clocked (a debugger Suspend pauses both tasks and the timebase).

The `!RS` (one priority-2 worker, 60 s) and `!RD` (two priority-2 workers, 60 s) modes are **unchanged** and remain appropriate for the safer, real-audio concurrency demonstration. **Never use `!RH` as a normal production hearing-aid feature**.

**One-core limitation:** The NUCLEO-L476RG has only a single Cortex-M4 core. FreeRTOS can time-slice two READY tasks at priority 7, but it can never execute them physically at the same instant. CubeIDE will legitimately show **only one** task in `RUNNING` when you suspend; the other is `READY`. Accumulated CPU time and changing work counters demonstrate concurrency.

## 1. Windows: exact import, Generate Code, preparation and build sequence

1. **KEEP YOUR WORKING R33 ZIP**. Terminate the current debug session, close the dashboard (and its COM port) and exit STM32CubeIDE completely.
2. Create a new directory such as `C:\\STM32\\R34_CONTINUOUS_SPIN`. Extract the R34 ZIP into it. Locate the INNER folder `261013-Individual_Demonstration_Connectivity` containing `.project`, `.cproject`, `.ioc`, `Core`, `Recovery`, `Dashboard`, `CHECK_PROJECT.py` and `PREPARE_PROJECT.bat`.
3. Launch CubeIDE with a fresh workspace, e.g. `C:\\STM32\\R34_workspace` (**File > Switch Workspace > Other**). A new workspace prevents a debugger launch from silently using an old project's ELF with the same name.
4. Select **File > Import > General > Existing Projects into Workspace**. Browse to the INNER project folder. UNCHECK **Copy projects into workspace**. Select the project and Finish. Do NOT import as a new blank STM32 project.
5. **If CubeIDE says Generate Code is required**, open the `.ioc` and run **Project > Generate Code** NOW. This step is OPTIONAL if there is no need for it. **Generate first, prepare second**. Do NOT change the clocks, TIM6, TIM2, priorities, tasks, DMA or FreeRTOS settings.
6. **Close STM32CubeIDE completely**, then double-click **`PREPARE_PROJECT.bat`** from the INNER project folder. This copies the local FreeRTOS middleware, patches the local `tasks.c` debugger profiler, and restores the canonical application sources and `.cproject` after any CubeMX generation. Require `PROJECT VALIDATION: 37 passed, 0 failed`, `R34 CONTINUOUS PRIORITY-7 SPIN ... PASS`, and `FINAL PREPARE PASS` before proceeding.
7. Reopen STM32CubeIDE in the R34 workspace. Right-click the imported project > **Refresh**. Choose **Build Configurations > Set Active > Debug**. Then **Project > Clean**, followed by **Project > Build Project**. Require **0 errors**; inspect all warnings. The standard Debug build contains `-g3` and the FreeRTOS `tasks.c` per-file `-O0` workaround. Do **not** globally change optimisation or alter profiler header files.
8. Connect Nucleo ST-LINK. Select **Run > Debug Configurations > STM32 C/C++ Application**. Verify the current project's Debug `.elf` and not an older R33 or R32 ELF. Debug/flash and press **Resume (F8)**.
9. Launch `Dashboard/START_DASHBOARD.bat`, connect to the ST-LINK USB virtual COM port at **115200, 8-N-1**, and send `!HI`. Require **`build=R34_PRIORITY7_CONTINUOUS_SPIN`**. If not, debug/flash path is incorrect.
10. **Never regenerate after PREPARE without repeating PREPARE afterward.** Correct repeat sequence: terminate debugging → CubeMX Generate Code (only if required) → close CubeIDE → PREPARE_PROJECT.bat → reopen/Refresh → Clean → Build → Flash.

## 2. Exact settings to VERIFY, not change

| Setting | Known-good target |
| --- | --- |
| Target MCU | NUCLEO-L476RG / STM32L476RGT6 |
| SYSCLK / HSE | 48 MHz / 16 MHz; PLLM=4, PLLN=24, PLLR=2 |
| HAL millisecond timebase | TIM6; RTOS SysTick separate |
| FreeRTOS tick | 1,000 Hz (`configTICK_RATE_HZ`) |
| Preemption | Enabled (`configUSE_PREEMPTION=1`) |
| Equal-priority time slicing | Enabled by FreeRTOS default when undefined (`configUSE_TIME_SLICING`) |
| RTOS heap | heap_4 with 32,768 B (project configuration) |
| Profiling | Existing 1 MHz TIM2 + current FreeRTOS runtime hooks; **UNCHANGED** |
| Debug | `-g3`, local FreeRTOS `tasks.c` `-O0` patch; rest as supplied |
| AudioTask / HighLoad7 priorities | **7 / 7** |
| ControlTask / BtTask | **6 / 5** |
| Background / CPULoadB | **2 / 2** |
| Scheduler bootstrap | Existing native `Demo_StartNativeRTOS`; do not add second CMSIS startup |
| UART | ST-LINK VCP 115200 baud, 8N1 |
| SAI/DMA/GPIO/BT/TC audio | Preserve all existing working settings |

**Do not create HighLoad7 in the CubeMX RTOS Tasks table.** It is created dynamically using `xTaskCreate()` by the existing native startup.

## 3. Convincing experiment — priority-7 continuous spin

1. Run Health and Bluetooth as normal **BEFORE the risky load test**. Capture evidence. Then for a fresh CPU usage baseline **reset the board or relaunch the debugger**, press Resume and obtain `!HI` / `!PV` with `valid=1`. A long previous uptime dilutes CubeIDE's *cumulative* Run Time percentages.
2. Open **Window > Show View > FreeRTOS > FreeRTOS Task List**. At baseline, verify `AudioTask` priority 7, `HighLoad7` priority 7 and HighLoad7 mostly `DELAYED` with little runtime. Leave the view visible.
3. In the serial command console, enter **`!RH`**. Verify ACK contains `mode=PRIORITY7_CONTINUOUS_SPIN|priority=7|delay_ticks=0|duration_ms=20000` (or click **CONTINUOUS SPIN · PRIORITY 7 · 20 s** in **LIVE RTOS TASK MONITOR**). The firmware transmits the ACK *before* enabling the spin task.
4. **Immediately—within roughly 2–5 seconds—use CubeIDE Suspend** and inspect the FreeRTOS Task List. With preemptive time slicing, HighLoad7 and AudioTask share CPU when both READY; their percentages should rise during the interval, though **not necessarily 50/50**. One will be `RUNNING`, the other `READY`. If HighLoad7 has zero runtime, verify the build marker and `!RH` ACK, not timer settings.
5. Expand **Expressions** and add `g_demo_debug` (or `g_r32_hi_work_blocks`). Check `high7_work_blocks` progresses across two **Resume → Suspend** observations during the active interval. CPU percentage in CubeIDE is cumulative; do not interpret an old 90% AudioTask figure as the share of the last two seconds.
6. **Resume (F8)**, let the board run until **20 seconds of MCU runtime** have elapsed. The spin task disarms itself without waiting for ControlTask, then sleeps when inactive. Normal UART replies and dashboard updates should resume. Send `!PV`, `!RM`, `!DG` and check progress, runtimes and any audio errors.
7. Do not expect `!RX`, `!PV`, `!RM`, `!DG` or dashboard polling to respond promptly DURING the spin. They are serviced by lower-priority ControlTask and may be delayed until cutoff. If recovery fails, **RESET the board** or terminate/reflash; the risky demonstration is explicitly optional.

**Audio continuation warning:** `AudioTask` has the *same* priority 7 and may time-slice with HighLoad7, but audio quality and DMA deadlines are not guaranteed under this synthetic saturation. This is a CPU scheduling test, not an audio quality pass criterion. For *production-friendly* preemption evidence, use **`!RD` + actual Bluetooth audio**, rather than `!RH`.

## 4. Commands and expected changes

| Command | Action |
| --- | --- |
| `!HI` | Must report `build=R34_PRIORITY7_CONTINUOUS_SPIN` |
| `!RH` | **Strict 20 s priority-7 continuous busy loop**, no active delay or yield |
| `!RS` | Existing priority-2 single-CPU workload 60 s |
| `!RD` | Existing two priority-2 CPU workers 60 s |
| `!RX` | Clears all flags **but may not execute during `!RH`**; self-cutoff is the recovery mechanism |
| `!PV` | Rolling CPU measurements, work counters, `hi7` status; may be delayed during `!RH` |
| `!RM` | Task/kernel runtime, including priority-7 load's accumulated execution |
| `!DG` | Audio/DMA error counters, inspect after test |

## 5. Validation boundary

Source-level project validation, C syntax/format tests, test harness and dashboard checks may pass; these do not replace a complete STM32CubeIDE ARM **build and physical hardware test**. CPU percentages, timing under interrupts, source restoration after local code generation, audible output, and behavior of `!RX` are ultimately hardware-dependent. This release deliberately does NOT modify the RTOS profiler and does NOT claim two literally simultaneous RUNNING threads on single-core STM32L476.
