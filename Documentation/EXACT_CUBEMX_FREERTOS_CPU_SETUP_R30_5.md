# R30.5 — exact CubeMX, FreeRTOS, build and CPU-evidence setup

**Board:** NUCLEO-L476RG / STM32L476RGT6, STM32CubeIDE 1.19.0 workflow.
**Project:** `261013-Individual_Demonstration_Connectivity`.

## Read this before changing anything

Your R30.4 telemetry proves: TIM2 runs (`clk=1`, `cen=1`, `psc=47`, `dt>0`), six FreeRTOS task records exist, task-loop counters advance, but neither the FreeRTOS kernel runtime accounting nor the context-switch hooks execute (`kernel=0`, `runsum=0`, `tsw=0`). This is **not** evidence that the RTOS scheduler is stopped. It is consistent with custom profiling definitions not reaching the `tasks.c` build. We cannot prove the precise cause until we inspect the compiled project or ELF.

**Important workflow rule: if you need Generate Code, perform it BEFORE running `PREPARE_PROJECT.bat`.** Code generation may overwrite custom RTOS configuration and native scheduler files. The preparation script restores the validated custom files and project compiler settings.

## Part A — start clean

1. Terminate the debugger (red square). Close serial sessions if they block flashing.
2. Extract the new ZIP to a clean, short path such as `C:\STM32_Demo_R305\` (avoid extracting over an earlier project).
3. In CubeIDE, remove older entries for `261013-Individual_Demonstration_Connectivity` **from the workspace only**, without deleting their files from disk.
4. Open the `.ioc` file from the fresh extracted folder. The following checklist is for **verifying** its existing settings, not redesigning the working SAI/audio peripheral configuration.

## Part B — CubeMX/ioc settings, item by item

| Item in CubeMX | Required value for this project | Why |
|---|---|---|
| MCU / target | STM32L476RGT6, NUCLEO-L476RG | Matches existing board and linker file |
| SYS → Timebase Source | **TIM6** | HAL millisecond tick; leave SysTick for FreeRTOS |
| Clock SYSCLK / HCLK | **48 MHz** | Matches proven working system and firmware output |
| HSE / PLL | HSE 16 MHz, PLLM=4, PLLN=24, PLLR=2 | Existing 48 MHz clock plan |
| APB1 / APB2 prescalers | Divide by 1; 48 MHz | TIM2 input clock is then 48 MHz |
| FreeRTOS interface | **CMSIS_V2** in `.ioc` | Metadata/generator interface; final firmware uses native FreeRTOS startup intentionally |
| FreeRTOS preemption | Enabled (`configUSE_PREEMPTION=1`) | Priority-based scheduling |
| FreeRTOS tick | **1000 Hz** | 1 ms RTOS tick |
| FreeRTOS total heap | **32768 bytes** (`heap_4`) | Existing validated heap configuration |
| FreeRTOS newlib reentrancy | **Enabled** | Existing project setting |
| FreeRTOS tickless idle | Disabled | Easier deterministic stress observation |
| TIM2 | **Do not assign it to HAL timebase or audio** | Custom 1 MHz run-time counter code owns TIM2 |
| TIM6 IRQ priority | **15** | Matches HAL tick implementation |
| SysTick IRQ priority | **15** | FreeRTOS tick |
| PendSV IRQ priority | **15** | FreeRTOS context switching |
| DMA1 Ch6 / DMA2 Ch1 priority | **5** | Current SAI DMA setup; preserve it |
| USART2 | **115200, 8-N-1** | ST-LINK virtual COM console |
| SAI1 / SAI2 / DMA configuration | **Leave unchanged** | Preserves working audio and shared clock |

**Task architecture:** The actual program starts `ControlTask` and creates `AudioTask`, `BtTask`, and `BackgroundTask` with native `xTaskCreate`. Their effective priorities are **6, 7, 5, 2**, respectively. The `.ioc` task entries are kept as metadata, **not** as a second scheduler. Do not add a second `osKernelStart()` or a second set of CMSIS-created tasks. FreeRTOS also manages `IDLE` and `Tmr Svc` tasks.

5. Only if CubeIDE requires it to resolve generated files, click **Project Manager → Generate Code** now. Accept the generation once and close the `.ioc` editor.
6. **Do not build or flash yet.**

## Part C — prepare the final application AFTER code generation

7. Open the on-disk project folder (containing the `.ioc`, `PREPARE_PROJECT.bat`, `Core`, and `Recovery`).
8. Double-click `PREPARE_PROJECT.bat` (or run from PowerShell). It restores custom `Core/Src`, `Core/Inc` and `.cproject`, and patches your local FreeRTOS `tasks.c` for Debug profiling.
9. Do not continue unless it says: `PROJECT VALIDATION: 37 passed, 0 failed`, `R30.5 INDEPENDENT TIM6 SAMPLED CPU-PROFILING CHECKS: PASS`, `PROFILE CONFIG VERIFIED`, and `FINAL PREPARE PASS`.
10. If the compiler requires another **Generate Code**, go back to step 5 and ALWAYS run `PREPARE_PROJECT.bat` again afterward.

## Part D — exact FreeRTOSConfig.h settings after PREPARE

Open `Core/Inc/FreeRTOSConfig.h` and verify:

```c
#define configUSE_PREEMPTION                     1
#define configUSE_TRACE_FACILITY                 1
#define configGENERATE_RUN_TIME_STATS            1
#define configUSE_STATS_FORMATTING_FUNCTIONS     1
#define configCHECK_FOR_STACK_OVERFLOW           2
#define configUSE_TICKLESS_IDLE                  0
#define INCLUDE_uxTaskGetStackHighWaterMark      1
#define INCLUDE_xTaskGetCurrentTaskHandle        1
#define INCLUDE_xTaskGetSchedulerState           1

extern void Demo_RuntimeStatsTimerInit(void);
extern uint32_t Demo_RuntimeStatsTimerRead(void);
#define portCONFIGURE_TIMER_FOR_RUN_TIME_STATS() Demo_RuntimeStatsTimerInit()
#define portGET_RUN_TIME_COUNTER_VALUE()         Demo_RuntimeStatsTimerRead()

extern void Demo_TraceTaskSwitchedIn(void *task);
extern void Demo_TraceTaskSwitchedOut(void *task);
#define traceTASK_SWITCHED_IN()  Demo_TraceTaskSwitchedIn((void *)pxCurrentTCB)
#define traceTASK_SWITCHED_OUT() Demo_TraceTaskSwitchedOut((void *)pxCurrentTCB)
```

You should find exactly one active `FreeRTOSConfig.h` on the compiler's include path (the intended project header is `Core/Inc/FreeRTOSConfig.h`). Do not rely on just seeing the text in a file if the compiler uses a different copy.

## Part E — exact STM32CubeIDE build and debugger settings

11. **Project → Properties → C/C++ Build → Settings**. Select the **Debug** configuration.
12. **MCU GCC Compiler → Debugging:** `-g3` (maximum debug info).
13. **MCU GCC Compiler → Optimization:** project default may stay **`-O2`** for audio performance. Do not change everything to `-O0`.
14. Ensure symbols **`DEBUG`, `USE_HAL_DRIVER`, and `STM32L476xx`** are defined in Debug.
15. Include paths must contain `../Core/Inc`, `../Middlewares/Third_Party/FreeRTOS/Source/include`, `../Middlewares/Third_Party/FreeRTOS/Source/portable/GCC/ARM_CM4F`, STM32L4 CMSIS and HAL includes.
16. The preparer patches **only** `Middlewares/Third_Party/FreeRTOS/Source/tasks.c` using a `DEBUG`-conditional compiler pragma with `-O0` and a retained `volatile ulTotalRunTime` symbol. It requires the `DEBUG` symbol. If setting an optimisation manually, the correct spelling is **`-O0` (capital letter O, digit zero)**, not `-0O`.
17. Confirm `Middlewares/Third_Party/FreeRTOS/Source/tasks.c` is in the project, compiled exactly once, and is not excluded from build. The normal FreeRTOS Cortex-M4F `port.c`, `heap_4.c`, and needed kernel sources must also compile.
18. Under **Run → Debug Configurations**, use the **Debug/*.elf from THIS fresh project**. The firmware build identity is printed by `!HI`.
19. Select **Project → Clean**, then **Project → Build Project**. Check the Console for errors/warnings and confirm the compiler builds `tasks.c`, `rtos_stats_timer.c`, `rtos_trace_proof.c`, and **`rtos_tick_proof.c`**.
20. Start Debug, flash the new ELF, then **Resume (F8)**. Leaving the CPU suspended will freeze RTOS counters and audio.

## Part F — raw test commands and expected observations

21. Open the ST-LINK virtual COM console at 115200. Send `!HI`:

```
R21|HELLO|PASS|...|build=R30_5_TICK_SAMPLING
```

If the build marker differs, stop: you are executing a different firmware build.

22. Send `!PX`. Required hardware fields: `clk=1`, `cen=1`, `psc=47` and `dt>0`. This means TIM2 is running at about 1 MHz. Your R30.4 data already confirmed this.
23. Send `!PV`; wait **2 seconds while the MCU is running**; send `!PV` again.
24. Inspect `ticks=...` on both `!PV` lines. The count should increase by approximately **2,000** over two seconds if the TIM6 interrupt is active. `tdrop=0` is expected with six tasks.
25. Check `method`:
    - `method=1` → real FreeRTOS kernel runtime counters (preferred).
    - `method=2` → real context-switch TIM2 instrumentation.
    - `method=3` → **1-kHz sampled task residency**, independent of the generated FreeRTOS trace hooks. This is an approximate CPU-utilisation estimate (± sampling error), not cycle-exact timing.
26. Check `valid=1|reason=0` after the initial warm-up. If `method=3` and values become valid, you can still prove the scheduler interleaves actual tasks; this **does not** automatically fix the CubeIDE Run Time (%) plugin's N/A field.

## Part G — lecturer demonstration, two experiments

27. **Quiet CPU test:** stop audio capture, then start the stress workload via the dashboard's **QUIET CPU TEST** button (or `!RX`, `!OX`, `!RS` as available). Measure `!PV` twice while running. The low-priority Background task should consume most spare CPU and IDLE should drop. Show `gt` and `work` increasing.
28. **Actual Bluetooth plus stress:** select direct Bluetooth `!BS`, start low-priority stress `!RS`, play real Bluetooth audio, check `!PV`, `!H`, and `!DG`. Audio/DMA callback counts should advance, with no new underruns. Background obtains only CPU not used by the higher-priority AudioTask. A previous observation of 93% AudioTask means a BackgroundTask cannot simultaneously consume 100% CPU on this one-core STM32.
29. Save timestamped RTOS and state-machine traces from the dashboard. The M1–M4 production controller is separately viewable in the **LIVE PRODUCTION STATE MACHINE** window; do not mistake direct Bluetooth `!BS` and telecoil `!TS` for production AUTO-mode FSM transitions.

## Part H — if anything is still broken

| Observation | Meaning / next action |
|---|---|
| `!HI` has wrong `build=` | Wrong firmware/ELF flashed. Fix Debug Configuration ELF path. |
| `!PX dt=0` | TIM2 hardware profiler stalled. Inspect RCC/TIM2 register settings. |
| `!PX dt>0`, `kernel=0`, `tsw=0`, `ticks` **increasing** | Native FreeRTOS runtime instrumentation still missing in build, but independent CPU sampling can work. Investigate what `tasks.c` and FreeRTOSConfig.h the compiler actually used. |
| `!PX dt>0`, `kernel=0`, `tsw=0`, **`ticks=0`** | TIM6 sampling callback or new source file did not make it into this build. Verify `HAL_TIM_PeriodElapsedCallback` and that `rtos_tick_proof.c` compiled. |
| `method=3`, `valid=1`, Run Time (%) is `N/A` | CubeIDE plugin still lacks readable native FreeRTOS counters. Use the measured `!PV` and Expressions values as evidence; do not falsely claim the viewer was fixed. |
| `method=1`, `kernel` advances, Run Time (%) still `N/A` | Debugger/plugin or retained `ulTotalRunTime` symbol issue. Check `tasks.c` optimization and debug symbol. |
| Task runtime showing Background near zero with Audio 93% | Scheduler is prioritizing AudioTask. Test CPU saturation with capture stopped, then independently test continuous audio under stress. |
| Preparatory validator fails | Do not flash. Send full PREPARE log and the file it flags. |

## Why the CubeIDE N/A field matters less than real evidence

STM32CubeIDE's FreeRTOS Task List relies on specific kernel symbols, runtime-statistics hooks and debugger integrations. ST's official UM2609 documents enabling `configGENERATE_RUN_TIME_STATS` with a fast timer and, for the `N/A` issue, making `ulTotalRunTime` volatile or using `-O0` specifically for `tasks.c`. Those actions are included in PREPARE. The separately labeled TIM6 method allows a real measured CPU estimate if CubeMX regeneration defeated the native hook configuration.

A Cortex-M4 executes one task at a time. Your goal is to demonstrate **concurrent preemptive scheduling**, not literal same-instant execution: when the Background task saturates spare CPU, a higher-priority audio task still resumes promptly whenever its work becomes ready.

## Final caution

The static validators and host-side tests are not a substitute for a full STM32CubeIDE ARM build or measurements on your real board. This version has not yet been proven on your physical hardware.
