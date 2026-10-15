# R30.3 — TIM2 profiler startup recovery

## Why this build exists

A repeated `!PV reason=3` means **the TIM2 hardware counter was unchanged over a measurement interval**. It does **not** mean that AudioTask or BackgroundTask are fake or stopped. The R30.2 timer was initialized only by a FreeRTOS run-time hook; this build also calls the timer initializer explicitly from `main()` after the 48 MHz system clock and peripheral setup. The initializer is idempotent so a later FreeRTOS hook cannot reset a running counter unnecessarily. A hardware sample across a 2 ms HAL delay is checked before scheduling.

`!PX` is new, intentionally read-only and independent of FreeRTOS's runtime snapshots. It measures TIM2 over two RTOS ticks and reports:

- `clk=1`: RCC peripheral clock enabled for TIM2
- `cen=1`: TIM2 counter enabled
- `psc=47`: for the default 48 MHz timer kernel clock, divides to 1 MHz
- `cnt0`, `cnt1`, `dt`: raw register readings and actual advancement; `dt` should be about 2,000 at 2 ms
- `init`, `rearm`: how often the timer was initialized or re-enabled
- `freeze`: whether DBGMCU was configured to freeze TIM2 when the CPU is halted

## Build and flash

1. Extract to a **new folder** named `261013-Individual_Demonstration_Connectivity`. Do not overwrite an older extracted copy.
2. Run `PREPARE_PROJECT.bat` and require `FINAL PREPARE PASS`. This sets up STM32Cube FW_L4 FreeRTOS and restores both the live and recovery sources.
3. Import **Existing Projects into Workspace**. Use Debug configuration. Clean and Build.
4. Do not click **Generate Code** after preparation. Flash the ELF from the new project's `Debug` directory and reset.
5. On USART2/ST-LINK VCP (115200, 8-N-1), expect `PROFILE: TIM2 1MHz counter advancing at startup` before the RTOS boot messages. If instead it says NOT COUNTING, collect `!PX` and stop changing optimization settings.

## Required command test

```
!HI
!PX
!PV
```

Wait two seconds, then:

```
!PX
!PV
```

Expected `!HI` identifier: `build=R30_3_TIM2_STARTUP`. Expected `!PX`: `clk=1`, `cen=1`, `dt>0` (normally ~2,000), and `cnt1>cnt0` (apart from unlikely 32-bit wrap). After warming up, `!PV` should have `reason=0`, `valid=1` if FreeRTOS task runtime accounting is advancing. It may show `reason=1` briefly for the first sample.

If `!PX` shows `clk=0`, the APB timer clock is gated; if `cen=0`, the timer is stopped; if both are 1 but `dt=0`, TIM2 is still not physically advancing and you should report the *complete* `!PX` response. The code never fabricates a CPU percentage if the hardware timer is stalled. The auto-repair only restores a disabled RCC/CEN bit, not a running timer's history.

## Demonstrate scheduling

1. With Bluetooth paused and no audio capture: `!OX`, then `!RX`, followed by two `!PV` reports. Check whether IDLE has spare CPU time.
2. Send `!RS` to start the low-priority arithmetic workload for 60 seconds. Compare `!PV` and `!RM` after 2–3 seconds: Background CPU share rises and IDLE falls if spare CPU time exists.
3. Send `!BS` to run real Bluetooth audio and start `!RS` again. Verify `!RM` DMA and speaker counters increase and that sound remains good. Background cannot get 100% when higher-priority audio consumes CPU.
4. In CubeIDE FreeRTOS Task List, suspend to inspect actual task names, states and stack high-water marks; use Expressions `g_demo_debug` for recent kernel-derived percentages. Debugger's Run Time (%) is a cumulative plugin view, not guaranteed to be populated by `g_demo_debug`.

## Source locations

- `Core/Src/main.c`: independent early initialization and raw boot timer test
- `Core/Src/rtos_stats_timer.c`: 1 MHz TIM2 setup, idempotent startup, limited clock re-enable
- `Core/Src/demo_app.c`: `!PX` raw diagnostic and `!PV` FreeRTOS sampler
- `Core/Inc/FreeRTOSConfig.h`: FreeRTOS runtime statistics integration
- `Recovery/`: exact restored source copies used by PREPARE_PROJECT

**Verification limitation:** Portable tests and ARM-targeted C syntax checks do not demonstrate the STM32L476 timer's physical behaviour. Only your real `!PX` output and CubeIDE build can confirm this on your board.
