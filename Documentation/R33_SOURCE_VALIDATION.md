# R33 source verification (offline only)

- Original CHECK_PROJECT.py base suite: 37/37 passed.
- R30.5 FreeRTOS TIM6 sampling, native runtime counters and R31/R32 guards: passed.
- R33 high-demand and mandatory rest/deadline source checks: passed.
- Embedded `Core/Src/demo_app.c`: host GCC C-syntax + printf-format checks passed, using STM32 HAL headers and FreeRTOS declaration stubs. (x86_64 host compilation, NOT ARM build.)
- Compiled isolated production HighLoad7 body: 5 consecutive 8 ms simulated timing windows, 5 mandatory 3-tick rests; PASS. Artificial clock only tests control flow; it is NOT hardware runtime/CPU evidence.
- Dashboard: Python compilation and Tk rendering with sample telemetry; six task cards and HighLoad7/Audio bars; PASS. Sample telemetry is not hardware evidence.
- Existing high-importance RTC/FreeRTOS/DMA/audio/states `.c/.h` and Debug `.cproject` unchanged compared with R32, except `demo_app.c` (and mirrored Recovery), dashboard, checks, docs and release marker.
- STM32CubeIDE ARM compilation, ST-LINK flashing, physical CPU loading, audio integrity and profiler percentages: **not verified here**. Check them using R33_HIGH_DEMAND_IMPORT_AND_TEST.md.
