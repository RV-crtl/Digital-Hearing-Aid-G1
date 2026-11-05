# R26.3 verification record (2026-11-04)

- R26 source verification, integration, controls and DSP regressions: **PASS** (`R263_SOURCE_TESTS.log`). Legacy TC LOW16 regression tests intentionally verify the *fallback*, not the new default.
- R26.3 LOW24 portable PCM regression: **PASS** (signed conversion, stereo word interleave, synthetic 1 kHz signal); included in the full source suite.
- ARM Cortex-M4F Clang 78/78 compilation and LLD relocatable object link: **PASS** (`R263_ARM_OBJECT_CHECK.log`). These are syntactic/object and duplicate-definition checks, not an executable link with newlib.
- Boot defaults and R34 transport identity: **PASS**. Live and recovery `demo_app.c` are identical. R34 SAI, PCM transport and S24 audio conversion source files are byte-for-byte equal to their R26.3 counterparts.
- No native STM32CubeIDE GCC ELF has been compiled in this environment; no ST-LINK test, physical microphone/telecoil testing, measured PCM capture, or sound-pressure test has been conducted. Physical 1 kHz listening result is **unknown**.

Do not interpret successful simulated PCM/host tests as a guarantee the PCM1808 is left-/right-justifying the real samples in the assumed container bits.
