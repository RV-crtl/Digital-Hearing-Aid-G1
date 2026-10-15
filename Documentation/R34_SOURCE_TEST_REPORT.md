# R34 validation and limitation record

- Baseline and release checks: `CHECK_PROJECT.py`, 37/37 + R34 guards passed.
- Critical file preservation: 18 profiler, STM32 startup, project configuration, RTC/clock/SAI/DMA, application DSP and FSM files are byte-identical to R33.
- `Core/Src/demo_app.c` and `Recovery/Core/Src/demo_app.c` match exactly.
- Host C syntax and format check: succeeded using STM32 HAL/CMSIS headers and local FreeRTOS API stubs. Host 64-bit pointer cast warnings in vendor headers are expected and unrelated to the target. Full ARM compilation not executed.
- Compiled host spin test: task performed repeated arithmetic with zero voluntary delays/yields in active mode and independently stopped once simulated HAL time passed deadline. This tests algorithm mechanics and not the STM32 RTOS scheduler.
- Dashboard Python compile and offscreen UI test: passed, including the 6 worker task cards. Screen mock-data tests are not hardware measurements.
- ZIP integrity checked after packaging.
- Physical STM32 flash, UART under priority starvation, real CPU runtime percentages, and Bluetooth audio continuity remain to be tested by user. A ControlTask at priority 6 is expected to be blocked while priority-7 worker remains continuously ready, so `!PV`, `!RX` and dashboard updates may stall until the 20 s self-stop.
