# Verification status of Group G1 release

## Checks actually executed in the authoring environment

- R34 baseline `CHECK_PROJECT.py`: all 37 baseline checks pass, including
  native FreeRTOS start, trace/tick profiling files and R34 busy-spin guards.
- New `CHECK_GROUP.py`: all 14 queue, integration, mirroring, project and PCB
  evidence checks pass.
- Portable C `group_features.c` tests: 5/5 pass with `gcc -Werror`.
- Portable C `group_encoder.c` tests: clockwise, counterclockwise and
  invalid-edge case pass with `gcc -Werror`.
- Full `demo_app.c` host **syntax only**, using STM32 HAL/CMSIS headers and
  explicitly mocked FreeRTOS types, passes with GCC `-Wall -Wformat=2`.
- Python dashboard/project validator bytecode compilation passes.
- ZIP CRC/manifest integrity checked.

## Important limitations

- Host C compiler + mock RTOS declarations are **not** an ARM assembler/linker
  or hardware execution test. Firmware deployment, interrupt behavior,
  CPU/stack/heap margins and audio fidelity have NOT been tested on STM32.
- The project preserves R34 48 MHz Nucleo configurations and old state-machine
  processing; custom PCB requires an explicitly ported build target.
- Default group DSP bypass preserves baseline signal; GROUP B1 action/serial
  activation requires actual hardware verification. The additional audio
  group processing is NOT calibrated for safe hearing-aid output SPL.
- G1 scene observation is a deterministic loudness heuristic, not TinyML.
- PDM microphone / USB audio / battery charging / BLE phone controls /
  two-mic beamforming are **not integrated into the flashed firmware**.

## Test commands (on Nucleo once built)

`!HI` -> group marker; `!GP` -> initial enabled=0; short press USER B1
-> `!GP` should show enabled=1 and next profile; long B1 press -> auto=1;
`!G2` -> MUSIC, `!G3` -> WIND, `!GOFF` -> original DSP bypass;
`!BS`/`!BC`, `!TS`/`!TC` -> real audio tests; `!PV` -> profiler real
measurement; STOP and diagnose on increased overrun/underrun or audible
quality loss.
