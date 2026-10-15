# Original group-member prototype sources (reference only)

These are snapshots of supplied prototypes. Their `Core/Src/main.c`, their
CubeMX-generated peripheral initialisation and separate RTOS entry points
are **not compiled into the active CubeIDE target**. They are included for
traceability and modular porting. Do not copy their `main.c`, `FreeRTOSConfig.h`
or `MX_FREERTOS_Init()` over the R34-based project.

- `Microphone_DFSDM`: PDM acquisition/DMA prototype needing pin/clock port.
- `Audio_RTOS_Output`: CMSIS-RTOS2 output task/queue prototype for reusable
  algorithms and synchronization patterns; not a second scheduler.
- `I2C_Touch_Probe`: address `0x5A` probe; this is not a verified rotary driver.
- PD5 portability layer is **already compiled** in `Core/Src/connectivity_*.c`.

Current group G1 features are in `Core/Src/group_features.c` and the event
queue additions to `Core/Src/demo_app.c`.
