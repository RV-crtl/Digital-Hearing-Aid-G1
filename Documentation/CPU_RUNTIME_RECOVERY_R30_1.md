# R30.1 — Recover the CubeIDE CPU runtime view (without changing audio firmware)

## The actual diagnosis

The **unmodified R30 ZIP** validates 37/37 locally. A Windows PREPARE result of 36/37 means the **working directory's dashboard/validator files differ** from the packaged baseline. R30.1 makes the HD-validator check independent of the exact label text and prints the full root folder path.

The FreeRTOS `Run Time (%)` column is **not** populated by `g_demo_debug`. That global is a second, independent, FreeRTOS-based measured telemetry view. `N/A` may be an IDE debugger-symbol issue. The STM32CubeIDE manual (UM2609) describes two remedies for `ulTotalRunTime` in FreeRTOS `tasks.c`: mark it volatile, or build `tasks.c` at `-O0`. PREPARE applies both remedies to the local FreeRTOS source, and the firmware's clock-profiler already uses TIM2.

## Installation (no more code regeneration)

1. Terminate Debug and close the dashboard. In CubeIDE, remove the previous project with the **same** name from Project Explorer **without ticking delete contents on disk**.
2. Extract `261013-Individual_Demonstration_Connectivity_R30_1_PROFILE_RECOVERY.zip` into a NEW directory, such as `C:\STM32\Connectivity_R30_1\`. The extracted project folder must directly contain `.project`, `.cproject`, `PREPARE_PROJECT.bat`, and `CHECK_PROJECT.py`.
3. Run `PREPARE_PROJECT.bat` once and read the printed **Project root**. Require `PROJECT VALIDATION: 37 passed, 0 failed` and `FINAL PREPARE PASS`. Otherwise STOP; do not try to rebuild or flash.
4. CubeIDE > File > Import > Existing Projects into Workspace. Import this freshly extracted project. Select Debug configuration.
5. **Do not click Generate Code**: the native FreeRTOS startup and recovery files are already prepared. Select Project > Clean > Build Project. Check that `tasks.c` recompiles. The Debug ELF must be under the new project's `Debug` directory.
6. **Terminate any old Debug launch**. Choose Run > Debug Configurations. Confirm **C/C++ Application** points to the newly compiled `Debug/261013-Individual_Demonstration_Connectivity.elf` in the new directory. Launch and resume execution.
7. ST-LINK Virtual COM, 115200 8N1: send `!HI`, expecting `build=R30_RUBRIC_EVIDENCE` (the firmware binary itself is unchanged from R30). This marker is not unique to R30.1; verify the ELF path as in step 6.

## Prove actual CPU accounting *before* worrying about N/A

8. Send `!PV` after the board has run for 2 seconds. Repeat 2 seconds later. A healthy sampler gives `R29|PROOF|DATA|...|valid=1|cpu10=...|bg10=...|idle10=...`. `cpu10=985` means 98.5% non-IDLE CPU time during a recent sample window. The `seq` field should increase between requests. If `valid=0` on repeated samples, the firmware's runtime sampler has not acquired valid intervals; investigate timer/counter operation instead of GUI formatting.
9. Open Debug > Window > Show View > Expressions, **manually add** `g_demo_debug`, and expand it after pausing the target. The variable is declared in `Core/Src/demo_app.c` and its type in `Core/Inc/connectivity_demo_debug.h`; an empty Expressions view does not indicate that the variable is absent. Look for `signature=0x52544F53`, `valid=1`, `sample_number` and the `*_cpu_pct_x10` members.
10. If GDB reports symbol not found, verify the Debug ELF symbol using CubeIDE's debugger console: `info address g_demo_debug`. Or from your ARM GCC tools: `arm-none-eabi-nm -a Debug/261013-Individual_Demonstration_Connectivity.elf | findstr g_demo_debug` in Windows command prompt, run from the project root. No symbol -> wrong ELF, wrong build, or symbol/link error.
11. `TIM2->CNT` should advance by roughly 1,000,000 per second. Read it at two separate debugger suspensions, or use a Memory view at `0x40000024` (TIM2 CNT register). Do not rely on a paused target to advance the timer.

## Enable CubeIDE FreeRTOS Task List percentages

12. Open Window > Show View > FreeRTOS > FreeRTOS Task List. Resume for several seconds and Suspend. `Run Time (%)` should be present if this CubeIDE version recognizes the kernel's profiling variable. Stack checking is a different toolbar toggle: it can show `Min Free Stack` even when runtime is N/A.
13. If Run Time remains N/A **while `!PV valid=1`**, FreeRTOS profiling is operating. Make sure `tasks.c` really compiled from `Middlewares/Third_Party/FreeRTOS/Source/tasks.c` inside the newly imported project. Do not change the whole project to `-O0` simply to force this display; the prep script already retains `ulTotalRunTime` and applies local `-O0` to `tasks.c`. The remaining problem may be debugger view compatibility/stale ELF metadata.
14. If both `!PV` and the IDE show invalid/no CPU measurements, send the outputs of `!HI`, two `!PV` calls, `!RM`, and the **first** compilation error/warning from the new build, or the ELF symbol checks; those distinguish bad firmware profiling from an IDE-only problem.

## Real load test

15. With audio stopped, send `!RX`, wait a second, send `!PV`. Send `!RS`, wait 3 seconds, `!PV` again: `load=2`, `bg10` should increase, `idle10` fall, and `cpu10` move toward 1000, but these effects depend on how much higher-priority processing is active.
16. Keep real Bluetooth playing (`!BS`), repeat `!RS` and the `!PV` sampling, and monitor audio plus `!DG`. A higher-priority AudioTask may consume most CPU; BackgroundTask must **not** be given higher priority merely to fabricate a big number.
17. Send `!RX` and verify IDLE CPU share recovers when other activity is low. CubeIDE Task List is a **halt-time snapshot**, not a continuously streaming live timeline; the dashboard or repeated `!PV` provides measurements while the device runs.

## Limitations

- R30.1 changes validator text and preparation diagnostics only. Bluetooth, telecoil, DSP, FreeRTOS scheduling, SAI/DMA and compiled firmware remain byte-for-byte unchanged from R30.
- Local static validation 37/37 is not a replacement for ARM build, real `!PV` readings, or running audio on hardware.
- Do not equate `N/A` with failed FreeRTOS scheduling; the actual Task List already shows created tasks and their states.
