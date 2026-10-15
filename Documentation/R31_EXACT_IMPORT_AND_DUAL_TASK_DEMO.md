# R31 — EXACT STM32CubeIDE IMPORT, GENERATION, BUILD AND LIVE DEMONSTRATION

**Target**: STM32CubeIDE 1.19.x, STM32L476RG Nucleo, STM32Cube FW_L4 **V1.18.2**, ST-LINK VCP, Windows.

**Important**: R30.5 is the verified working CPU-profiler baseline. R31 only adds a priority-2 `CPULoadB` task, a `!RD` dual-load command, related counters and dashboard controls. **Do not edit** `FreeRTOSConfig.h`, TIM2, TIM6, runtime trace macros, audio priorities or the PLL. Avoid manually selecting overall `-O0` in the project: profiling has a special Debug-only optimisation workaround for `tasks.c` only.

## A. Clean install in CubeIDE (recommended, exact order)

1. **Stop debugging** any old firmware, close the serial dashboard/COM port. Exit CubeIDE.
2. Create a short Windows path such as `C:\STM32\R31_DUAL`. Unzip the entire R31 ZIP **there**, preserving the internal folder `261013-Individual_Demonstration_Connectivity`. The actual project folder must contain `.project`, `.cproject`, `.ioc`, `Core`, `Dashboard`, `Recovery`, and `PREPARE_PROJECT.bat` directly. Do not open the ZIP itself as a project and do not create a project from scratch.
3. Launch STM32CubeIDE and choose **File → Switch Workspace → Other...**, e.g. `C:\STM32\R31_workspace` (separate from old R30 projects). This avoids the duplicate-project-name problem.
4. **File → Import → General → Existing Projects into Workspace → Next**. Click **Select root directory → Browse** and choose the **inner** `261013-Individual_Demonstration_Connectivity` directory (not the outer extracted folder). Tick the identified project; **untick Copy projects into workspace**. Click **Finish**. The project appears in Project Explorer.
5. If CubeIDE requires source generation to create its middleware/project files, do that **ONCE NOW**, by double-clicking the `.ioc` and clicking **Project → Generate Code**, allowing the normal STM32CubeL4 pack to install. **Do not change any configuration in the `.ioc`.** If CubeIDE does not require regeneration, skip this step. Either way, the next step is mandatory.
6. **Close CubeIDE completely** so project settings aren't held open. In File Explorer, open the **inner project folder** and double-click **`PREPARE_PROJECT.bat`**. This copies local FreeRTOS from your STM32CubeL4 repository, applies the profiler debugger workaround, and **restores our firmware after any Generate Code step**. Wait for `PROJECT VALIDATION: 37 passed, 0 failed`, the additional R31 checks, and `FINAL PREPARE PASS`. **Never build or flash if PREPARE failed.**
7. Reopen STM32CubeIDE using the R31 workspace. Right-click the project → **Refresh (F5)**. Open **Project → Properties → C/C++ Build → Settings** and choose **Configuration: Debug**. Keep project compilation at its supplied default (usually `-O2`), with **debug info `-g3`**. FreeRTOS `tasks.c` uses `-O0` for debug automatically through the supplied workaround. Do **not** reset `.cproject` or retune the compiler.
8. Right-click project → **Build Configurations → Set Active → Debug**. Choose **Project → Clean...**, check **Clean all projects** or this project, wait for clean, then **Project → Build Project**. Under Console require **0 errors**. Previous implicit-function-declaration warnings are addressed by the missing `#include "rtos_stats_timer.h"` in `demo_app.c`. If you get a new error, save the full build log instead of changing random CubeMX settings.
9. To flash: connect Nucleo ST-LINK USB, choose the imported R31 project and **Run → Debug Configurations... → STM32 C/C++ Application**, select or create the launch for this project, and confirm the **C/C++ Application** points to the *new project* `Debug/261013-Individual_Demonstration_Connectivity.elf` (not the previous R30 directory). Click Debug / Resume (F8).
10. **While running**, start `Dashboard/START_DASHBOARD.bat`, select the **ST-LINK virtual COM port**, connect at **115200 8-N-1**, enter `!HI`. Confirm `build=R31_DUAL_SCHEDULER`. If you see R30, the wrong ELF was flashed; stop and fix the launch path.

**If you ever click Generate Code again**, STOP → close CubeIDE → run `PREPARE_PROJECT.bat` again → reopen → Refresh → Clean → Build → Flash. **The preparation script must be the last operation that changes firmware/project files before you compile.**

## B. Settings to CHECK (do not change working ones)

| Where | Exact expected setting |
|---|---|
| STM32CubeMX MCU | NUCLEO-L476RG / STM32L476RGT6 |
| RCC HSE | 16 MHz; PLL clock source HSE |
| PLL | PLLM=4, PLLN=24, PLLR=2; SYSCLK=48 MHz |
| SYS → Timebase source | **TIM6** (not SysTick) |
| TIM6 interrupt | Enabled; priority 15 as supplied |
| FreeRTOS .ioc interface | CMSIS V2 metadata; **do not add a second CMSIS scheduler** |
| Native kernel boot | Existing `Demo_StartNativeRTOS()` starts the scheduler once |
| `configUSE_PREEMPTION` | `1` |
| `configUSE_TIME_SLICING` | Leave undefined (FreeRTOS default is `1`); do **not** disable it |
| `configTICK_RATE_HZ` | `1000` Hz; one millisecond per time slice |
| `configTOTAL_HEAP_SIZE` | 32768 bytes; do not enlarge without measuring RAM |
| Tickless idle | Disabled |
| `configGENERATE_RUN_TIME_STATS` | `1` |
| `configUSE_TRACE_FACILITY` | `1` |
| TIM2 runtime profiling | **1 MHz** counter, initialized by `rtos_stats_timer.c` |
| `tasks.c` in Debug | **`-O0` only for the kernel compilation unit** (supplied workaround) |
| Debug info | `-g3`, do not strip symbols |
| HAL audio / peripherals | Leave all DMA, SAI, UART clocks and GPIO exactly as shipped |

**Important CubeMX nuance:** The `.ioc` contains a legacy FreeRTOS task metadata table with values 32/40/24/8. It is **not** the authoritative scheduler configuration; the native code creates `ControlTask` priority 6, `AudioTask` priority 7, `BtTask` priority 5, `Background` priority 2, and new `CPULoadB` priority 2. Seeing `CPULoadB` in the live debugger requires no new CubeMX task entry. Don't regenerate or try to make CubeMX create a second `CPULoadB` task.

## C. Exactly what to press for the dual-task demonstration

1. Run `!HI` and verify `build=R31_DUAL_SCHEDULER`. Run `!H` (task loop counts moving). Run `!PV` twice about two seconds apart (`valid=1` desirable; **CubeIDE CPU percentages are independently useful**).
2. Start CubeIDE Debug, **Resume F8** to allow the system to execute. Open **Window → Show View → FreeRTOS → FreeRTOS Task List**. If the view is missing, use **Window → Show View → Other... → FreeRTOS Task List**. Enable *Toggle Stack Checking* if necessary. Suspend momentarily only when you wish to inspect; press Resume again afterwards.
3. Open **LIVE RTOS TASK MONITOR** on dashboard. Click **DUAL QUIET (NO AUDIO)**. This sends `AUDIO STOP` followed by `!RD`: the two priority-2 workers execute continuously for up to **60 s**.
4. Watch **CPU A** and **CPU B** bars, work counters and task-runtime percentages; both **work Δ** values should be >0 and their runtime should rise. ControlTask still responds to commands. When one worker is shown RUNNING in CubeIDE, the other will be READY, not RUNNING; that is correct for a **single-core CPU**.
5. Click **STOP CPU STRESS / LOAD** (sends `!RX`). Then click **BT + DUAL LOAD**, which sends `!BS` and then `!RD`. Start physical Bluetooth music/speech. **Both workers share leftover CPU only**; higher-priority AudioTask preempts them. Check that `!DG` shows live SAI/DMA and no rising underruns/overflows, and that playback remains audible.
6. Click **SAVE RTOS TRACE** to export timestamped measured results; take one CubeIDE screenshot with both priority-2 tasks and nonzero runtime. The demonstration is concurrent time slicing (not simultaneous multicore execution).
7. After each 60-second stress mode, the firmware automatically stops both loads. Press `!RX` whenever you need to stop early. Return to regular Bluetooth/telecoil demos.

### Direct command alternative (without dashboard)

| Command | Effect |
|---|---|
| `!RS` | Original **single** CPU-bound Background task, priority 2, 60 s |
| `!RD` | **Dual** CPU-bound Background and CPULoadB, both priority 2, 60 s |
| `!RX` | Stop single or dual CPU stress immediately |
| `!RM` | Actual task state, stack reserve, runtime, `bg/work` and new `b2/w2`, `b2run` |
| `!PV` | Real debug snapshot, including `b2cpu10` (tenths of percent), `b2/w2` and `dual` |
| `!PX` | TIM2 hardware counter diagnostic |

To prove equality fairly, compare runtime **deltas over a few seconds**, not lifetime percentages right after switching modes. Under live audio, workers may receive unequal or low percentages because AudioTask can dominate the CPU. This is not an error.

## D. If it doesn't work

- **`!HI` reports R30** → Debug launcher points to the wrong `.elf`, or you flashed an old directory.
- **`!RD` returns FAIL: dual_tasks_not_created** → Check the boot UART warning for failed `CPULoadB` creation; confirm a 32 KB FreeRTOS heap and sufficient free RAM; don't blindly enlarge stacks/heap.
- **`!RM` has no `b2`/`b2run` fields** → Old firmware flashed. Do NOT change the CPU profiler.
- **`!PV valid=0` but FreeRTOS Task List shows runtime** → Check whether the timer snapshots need warm-up, then compare native task runtimes; do not immediately reinstall FreeRTOS.
- **`Run Time (%)` returns N/A** → Check that you ran PREPARE after Generate Code, Debug `tasks.c` is `-O0`, Debug build has `-g3`, and correct ELF is selected. Do not edit TIM2/TIM6.
- **Audio distorted or DMA errors rise** → Stop stress `!RX`, preserve log, and do not claim real-time safety. The two priority-2 tasks should not be promoted to priority 7.
- **Dashboard fails to open** → Run `Dashboard/START_DASHBOARD.bat`; it installs `requirements.txt` using Python/pip. Use Python 3 and allow the installation.

**Verification boundary:** This R31 release was checked with source validation, Python compilation, an offscreen dashboard smoke test and firmware syntax checking using native STM32 headers and FreeRTOS API stubs. It has not been built with your exact ARM toolchain or executed on your STM32. Real scheduling percentages and audio integrity must be confirmed on the board.
