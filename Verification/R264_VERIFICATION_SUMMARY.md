# R26.4 verification summary — 2026-11-04

## Passed
- Full R26 source regression suite (`R264_SOURCE_TESTS.log`).
- Host-compiled **production** signed-24-bit recovery C module with synthetic 1 kHz inputs, noise-floor continuity, large signals and positive/negative polarity (`R264_PORTABLE_TEST.log`).
- Boot settings, optional diagnostic A/B modes, recovery copies, unchanged SAI 24-bit/32-bit DMA (`R264_INTEGRATION_TEST.log`).
- 79/79 Cortex-M4F targeted Clang object compilations and a relocatable object link (`R264_ARM_OBJECT_CHECK.log`).

## Not tested / not claimed
- STM32CubeIDE 1.19.0 ARM GCC/newlib executable ELF build or board flashing.
- Real PCB input amplitude, I2S word alignment, periodic hum frequency, analog noise, SPL/output safety and RTOS timing.
- Real-world telecoil signal detection or improvement. Target gain is a bounded experiment, not a validated acoustic calibration.

The implementation does not gate or notch the telecoil signal. It necessarily amplifies any coincident analog noise along with the wanted signal. The debug TC PROBE and TC 24 STATUS commands can differentiate data-word problems and level-control problems.
