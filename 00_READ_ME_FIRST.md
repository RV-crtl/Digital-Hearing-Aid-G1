R34: OPTIONAL CONTINUOUS PRIORITY-7 SPIN (20 s), no active task delay/yield.

START HERE: Documentation/R34_CONTINUOUS_SPIN_IMPORT_GUIDE.md

The user observed HighLoad7 taking <1-5% CPU in the prior R32/R33
burst-and-rest versions. In R34, !RH commands HighLoad7 to stay READY and
perform real arithmetic continuously for up to 20 seconds; it competes with
AudioTask (both priority 7). There is NO vTaskDelay() or taskYIELD() inside the
active spin. It automatically turns off after 20 s using the TIM6 HAL tick.

IMPORTANT RISK: The priority-6 ControlTask / UART command handling and the
priority-5 BtTask may be starved during those 20 s. The dashboard can freeze
and !RX may not be serviced until the self-stop. Use CubeIDE Suspend during
the active run, then Resume and wait for recovery. RESET board if necessary.
Do NOT claim two physically simultaneous RUNNING tasks: STM32L476 has one core.

The existing safer !RS and !RD priority-2 CPU-load modes are preserved.
CPU profiler, TIM2, TIM6, FreeRTOSConfig.h, tasks.c patch, DMA/SAI, audio,
state machines, scheduler startup and priorities are unchanged from R33.

SETUP ORDER: If Generate Code is required, do it FIRST, then CLOSE CubeIDE,
run PREPARE_PROJECT.bat, require 37/37 + R34 check + FINAL PREPARE PASS,
reopen CubeIDE, Refresh, Debug configuration, Clean, Build, Flash.

This release is NOT hardware-verified here. Preserve R33 as a rollback.
