# Exact STM32CubeIDE import and flash order (Group G1 — Nucleo only)

**Hardware target:** NUCLEO-L476RG / STM32L476RGT6 with the known working
R34 Bluetooth/telecoil/PCM1808/MAX98357 wiring. Custom PCB port is NOT ready.

1. **Back up R34** so you always have the working individual demonstration.
2. Extract this ZIP into a clean short path, e.g.
   `C:\STM32\GROUP_G1\261013-Individual_Demonstration_Connectivity`.
   Import the **inner project directory** containing `.project` and `.cproject`.
3. In STM32CubeIDE select a **new workspace**, e.g. `C:\STM32\GROUP_G1_WS`.
4. `File > Import > General > Existing Projects into Workspace`; select the
   inner project folder. **Uncheck Copy projects into workspace.**
5. `Project > Build Configurations > Set Active > Debug`.
6. If CubeIDE insists on `Generate Code`, do that **before preparation**.
   Do not change MCU clock, SAI, DMA, TIM2, TIM6, FreeRTOS or UART settings.
7. CLOSE STM32CubeIDE, then run `PREPARE_PROJECT.bat` in the inner folder.
   This locates `STM32Cube_FW_L4_V1.18.x` installed by CubeIDE, restores
   source/recovery files and patches only local FreeRTOS `tasks.c` profiling.
   Require **37/37 passed**, **GROUP G1 VALIDATION PASS** and **FINAL PREPARE PASS**.
8. Reopen CubeIDE, right-click project `Refresh`. `Project > Clean` and
   `Project > Build Project`. **Do not flash on build errors.** Inspect warnings.
9. `Run > Debug Configurations` -> new **Debug ELF** from this project folder.
   Flash via ST-LINK, press F8/Resume.
10. If using ST-LINK VCP, open USART2 115200, 8-N-1. Send `!HI` and verify
    `group=G1_ADAPTIVE_BASE` (same working R34 marker remains present).
    Send `!GP`, `!GON`, `!G1`, `!GP`. Confirm the queue applies the profile.
11. Without a laptop, use the Nucleo USER B1: short press cycles profile,
    long press toggles automatic loudness mode; LD2 indicates enabled/auto.
12. Check original `!BS`, `!BC`, `!TS`, `!TC`, `!PV`, `!PX`, and actual
    speaker output. Record any change in underrun/overrun counts.

If you regenerate code **after PREPARE**, repeat step 7 then Clean/Build.
Do not add new application tasks in CubeMX: native FreeRTOS is the authoritative
scheduler. Preserve debug symbols `-g3`, profiler `tasks.c` Debug-only `-O0`
workaround, 1kHz TIM6 HAL timebase, 1MHz TIM2 profiling, 48MHz core and
SAI PLL settings until validated.

**The package is a source project, not a pre-flashed board image.** The final
ARM link and physical playback must still be performed on the target board.
