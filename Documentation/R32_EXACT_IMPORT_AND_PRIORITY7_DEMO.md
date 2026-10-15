# R32 — Exact STM32CubeIDE Import, Settings, and High-Priority Scheduling Demonstration

## IMPORTANT: What “two tasks running in parallel” means here

The NUCLEO-L476RG has ONE Cortex-M4 CPU core and NO Intel-style hyperthreading or simultaneous multithreading. FreeRTOS creates **software threads**, and preemption/time slicing interleaves them. **It is physically impossible for CubeIDE's Task List to correctly show two tasks in RUNNING state at the same suspended instant.** One will be RUNNING; another can be READY or DELAYED. Real concurrency evidence is two independently advancing work counters, non-zero kernel runtime for both tasks, and audio/DMA continuity.

**Preserve the known-good R30.5/R31 CPU profiler.** R32 changes only the production demo C task/reporting, its debug-struct append, dashboard and validation/notes. It does NOT modify `FreeRTOSConfig.h`, TIM2, TIM6, task-switch hooks, audio source DSP, main startup, peripherals, scheduler priority of existing tasks or `.cproject`.

## A. Start-to-finish setup (Windows + STM32CubeIDE)

1. **Stop your running debug session** (Terminate) and close the dashboard, serial port and CubeIDE. Keep a backup of the working R31 ZIP and do not replace it.
2. Create `C:\STM32\R32_PRIORITY7` and **extract this entire ZIP** there. Confirm the inner folder is `C:\STM32\R32_PRIORITY7\261013-Individual_Demonstration_Connectivity` and directly contains `.project`, `.cproject`, `.ioc`, `Core`, `Recovery`, `Dashboard` and `PREPARE_PROJECT.bat`.
3. Open **STM32CubeIDE**, choose **File → Switch Workspace → Other** and create e.g. `C:\STM32\R32_workspace` to avoid conflicts with other projects of the same name.
4. **File → Import → General → Existing Projects into Workspace**. Choose the **inner** project folder above. UNCHECK `Copy projects into workspace`. Click Finish. Do **not** use “New STM32 Project” or import the ZIP as an Eclipse archive.
5. **If CubeIDE demands Generate Code**, open the `.ioc` and run **Project → Generate Code exactly once, NOW**. Otherwise skip it. Do not adjust any clocks, FreeRTOS tasks or DMA. This is only to satisfy the CubeIDE generator when required.
6. **Close CubeIDE fully.** Run the inner folder's **`PREPARE_PROJECT.bat`** AFTER any generation. It restores all app/middleware/FreeRTOS configuration and Debug `.cproject` from Recovery, patches the local FreeRTOS `tasks.c` profiler and performs validation. Wait for `PROJECT VALIDATION: 37 passed, 0 failed`, `R31 DUAL EQUAL-PRIORITY... PASS`, `R32 PRIORITY-7... PASS` and `FINAL PREPARE PASS`. **Stop if anything fails.**
7. Reopen CubeIDE with the R32 workspace. Right-click project → **Refresh** (F5). Right-click → **Build Configurations → Set Active → Debug**.
8. Go to **Project → Clean**, choose this project; then **Project → Build Project**. Expect **0 errors**. Confirm `Core/Src/demo_app.c` compiles and the project links. Do not change overall optimisation from the supplied Debug project settings. The preparation script isolates the `tasks.c` debugger workaround; don't globally force `-O0`.
9. **Flash/debug:** Connect ST-LINK USB. Go to **Run → Debug Configurations → STM32 C/C++ Application**, select/create the new R32 launch; verify *C/C++ Application* refers to this project's `Debug/261013-Individual_Demonstration_Connectivity.elf`, not the R30/R31 ELF. Click **Debug**, then **Resume (F8)**.
10. Run `Dashboard/START_DASHBOARD.bat`, connect to the **ST-LINK VCP COM port** at **115200 baud, 8-N-1**. Send `!HI`. Require `build=R32_EQUAL_PRIORITY_DEMO`. If the old marker appears, stop: you flashed the wrong ELF.

**IMPORTANT ORDER:** if you ever use **Generate Code again**, **STOP Debug → Generate Code → CLOSE CubeIDE → PREPARE_PROJECT.bat → reopen / Refresh → Clean → Build → Flash**. PREPARE must be after Generate Code and before Build. Do not make conflicting changes to `FreeRTOSConfig.h`.

## B. CubeMX/CubeIDE settings: VERIFY only, do not change working ones

| Setting | Preserve this |
|---|---|
| Board | NUCLEO-L476RG / STM32L476RGT6 |
| SYSCLK | 48 MHz (16 MHz HSE, PLLM 4, PLLN 24, PLLR 2) |
| HAL timebase | TIM6 (not SysTick) |
| FreeRTOS kernel tick | 1000 Hz, SysTick via native FreeRTOS port |
| FreeRTOS GUI | CMSIS V2 metadata; actual boot is **native** `Demo_StartNativeRTOS()`; no duplicate scheduler |
| FreeRTOS preemption | `configUSE_PREEMPTION=1` |
| Time slicing | `configUSE_TIME_SLICING` enabled (FreeRTOS default when undefined) |
| Tickless mode | off |
| FreeRTOS heap | `configTOTAL_HEAP_SIZE=32768` bytes; R32 creates one additional 256-word task; watch heap and task creation warnings |
| TIM2 CPU profiler | 1 MHz, **unchanged** |
| Debug symbols | `-g3` |
| Debug FreeRTOS kernel `tasks.c` | `-O0` local workaround from `PREPARE_PROJECT.bat`, rest of source as supplied |
| AudioTask | priority 7 (existing, unchanged) |
| ControlTask | priority 6 (unchanged) |
| BtTask | priority 5 (unchanged) |
| Background and CPULoadB | priority 2 each (unchanged) |
| **HighLoad7** | **priority 7** (new optional task, blocked when idle) |
| Audio SAI / DMA / GPIO | **do not change** |

**DO NOT create HighLoad7 as a CubeMX task.** The application creates it once using `xTaskCreate` with priority 7. Creating another CMSIS task duplicates its work or breaks native startup.

## C. Four demonstration commands

| Command | Description |
|---|---|
| `!RS` | Existing single priority-2 load, 60 s automatic stop |
| `!RD` | Existing two priority-2 loads, 60 s automatic stop |
| **`!RH`** | **New priority-7 equal-priority test**, 350 μs bounded compute per burst, then block 2 RTOS ticks, 20 s automatic stop. AudioTask stays priority 7; stops old load modes first |
| `!RX` | Stop all three modes early (single, dual, priority 7) |
| `!RM` | Detailed kernel task runtime + new `HighLoad7` cumulative `h7run`, `h7work`, `h7state`, priority 7 |
| `!PV` | Rolling CPU window, `h7cpu10` tenths of percent, `hi7` active state, task work counters |

## D. Lecturer demonstration (6–8 minutes)

1. Resume firmware. Use `!HI` to verify R32. Check `!PX`: TIM2 running. Send `!PV` twice ~2 seconds apart, require `valid=1`. Open **Window → Show View → FreeRTOS → FreeRTOS Task List** in Debug. This displays live kernel tasks **only when the target is suspended**. Click **Resume** when you want counters to advance.
2. Show AudioTask priority 7, ControlTask 6, BtTask 5, Background 2, CPULoadB 2 and new HighLoad7 priority 7. Unloaded HighLoad7 is typically DELAYED and low CPU time.
3. Open the dashboard's **LIVE RTOS TASK MONITOR**. **DUAL QUIET (NO AUDIO)** drives `!RD` and demonstrates two priority-2 tasks sharing unused CPU time. Both CPU A and B progress, with runtime increasing. STOP with `!RX`.
4. Start real Bluetooth playback (`!BS` + physical audio), verify audible speaker output and DMA counters using `!DG`. Press **PRIORITY 7 · 20 s TIME-SLICING DEMO**, which sends `!RH`. Observe HighLoad7 CPU % and `h7work` while AudioTask continues. Depending on audio activity, the dummy could use substantially less than half the CPU; **do not claim a guaranteed 50/50 split**.
5. **Suspend** the debugger DURING the 20-second window. Expect **one** `RUNNING` task and HighLoad7 either `RUNNING`, `READY` or `DELAYED`. A single snapshot cannot show both simultaneously executing; show both **nonzero accumulated `Run Time (%)` values** and `!PV` intervals showing BOTH making progress. HighLoad7 is deliberately DELAYED for two ticks between bursts, to keep UART/control responsive.
6. Press **Resume** then **STOP CPU STRESS / LOAD** (`!RX`), or wait for automatic stop after 20 s. Confirm HighLoad7 progress counter stops increasing and its runtime share drops over new samples. BT output quality should remain good and underrun/overflow counters should not increase. If it distorts, stop immediately—this is a *scheduling demonstration*, not proof of audio safety at arbitrary CPU loads.
7. Save **RTOS TRACE** for independent evidence (MCU timestamps, both CPU task counts, real runtime, speaker/DMA counters). Optional: `!RD` + Bluetooth for the safer production preemption demo with priority-2 workers.

## E. Common mistakes

- **Hyperthreading?** No. Cortex-M4 is one core, one hardware instruction stream. The task switch is concurrency not simultaneous execution.
- **Both `RUNNING` while paused?** Impossible on a single core. An RTOS task list is a frozen snapshot, not a dual-core execution monitor.
- **HighLoad7 says `DELAYED`?** Expected while its mandatory 2-tick pause is active. Runtime and work counters prove the task has executed.
- **HighLoad7 takes 10–25% rather than ~50%?** Acceptable: bounded duty cycle protects the system; other higher/equal-priority work runs. Percentages depend on AudioTask activity and peripheral interrupts.
- **Command freezes?** Wait up to 20 s for auto-off; reset board if needed. Do not raise load burst duration or remove `vTaskDelay`.
- **`Run Time (%)=N/A` again?** Generate Code must precede PREPARE; use Debug build and correct ELF; `tasks.c` must compile `-O0` and keep the volatile profiler symbol. Do not modify TIM2/TIM6.
- **HighLoad7 absent?** Look for serial warning about insufficient FreeRTOS heap; `!RH` returns `FAIL|reason=high7_task_not_created`. Do not blindly increase heap beyond MCU SRAM.
- **Full physical validation pending.** The source checks and host C syntax/GUI smoke tests are *not* the same as a full ARM build, flashed runtime test, or verified audio quality.
