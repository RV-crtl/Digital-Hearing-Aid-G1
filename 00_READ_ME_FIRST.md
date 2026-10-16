# START HERE — Digital Hearing Aid Group Firmware

This repository contains a **Nucleo-L476RG FreeRTOS integration candidate** and
a separately supplied, **unverified custom KiCad PCB design**.

**[Open the main project README](README.md)** — overview, system diagram, exact status
and two-click links for group members.

| Need | Go to |
| --- | --- |
| Import/build in STM32CubeIDE | [Exact import guide](Documentation/GROUP_G1_IMPORT_AND_BUILD.md) |
| Find code functions, task ownership and source files | [Code map](Documentation/CODE_MAP.md) |
| Run real production-C mathematical tests | [Verification guide](Validation/README.md) |
| Understand PCB/power limitations | [Hardware guide](Hardware/README.md) |
| Present without a laptop | [Standalone showcase](Documentation/STANDALONE_SHOWCASE.md) |
| Know what is physically verified | [Validation ladder](Documentation/VALIDATION_LADDER.md) |

**Safety / stability:** `!RH` activates a deliberately disruptive priority-7
CPU-spin experiment (up to 20 seconds). It is *not* for the standalone product demo.
No firmware in this repository is certified to limit acoustic output SPL.

**Build order:** If CubeIDE requires CubeMX **Generate Code**, do that first,
close the IDE, run `PREPARE_PROJECT.bat`, reopen, refresh, clean, build and flash.
Do not flash this Nucleo pin configuration on the custom PCB without the pin,
clock, power and peripheral audit.

Prior versions are recoverable from Git history and the dedicated
`archive/g1-baseline-before-g4-20261017` branch.
