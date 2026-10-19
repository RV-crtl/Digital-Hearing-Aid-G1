# Operating firmware vs laboratory-stress firmware

**Default for the system demonstration:** `DHA_ENABLE_RTOS_STRESS=0` in `Core/Inc/dha_build_mode.h` (and mirrored Recovery copy). The compiled operating image must create **AudioTask** and **BtTask** alongside existing **ControlTask** and FreeRTOS system tasks. It does **not** create Background, CPULoadB or HighLoad7 experimental workers. Even if UART is connected, stress commands `!RA`, `!RS`, `!RD`, `!RH` are rejected. Laboratory self-test command paths are not callable in operating mode.

**Laboratory research only:** explicitly add `-DDHA_ENABLE_RTOS_STRESS=1` in the corresponding CubeIDE build configuration. The lab build then creates the extra three stress workers. Never flash this build for the system demonstration. The HighLoad7 priority-7 20-second spin is intentionally disruptive and may starve UI; it is inconsistent with operating-product reliability.

This build gate is **source-level**. Before relying on it, check the actual ELF map for task/diagnostic presence and run a full STM32 ARM cross-compile. We cannot claim the ELF was physically built/loaded because only GCC/Clang portable host checks were available during preparation.

**Do not perform unit tests on the MCU during the demonstration.** Source-based unit tests under `Tests/` are host-only. Dynamic runtime health counters are permitted as engineering diagnostics; they do not substitute for the rubric's observed external behaviour.

**Exact acceptance prior to viva:** manually check normal product loop responsiveness to physical B1 gestures and Bluetooth/telecoil transitions with ST-Link and UART detached; perform real power and acoustic measurements; note firmware SHA in evidence register. G5 PC13 interactions do not migrate automatically to the custom PCB.
