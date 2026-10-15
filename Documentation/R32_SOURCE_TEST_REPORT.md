# R32 validation record

- R31 critical file comparison: 15 configuration/boot/profiler/audio files unchanged, byte-for-byte.
- `CHECK_PROJECT.py`: original 37 checks passed, plus native task, runtime and R32-specific source assertions.
- `demo_app.c`: host GCC C syntax and printf format check passed with native STM32 HAL headers and lightweight FreeRTOS stubs. This is **not** a full ARM cross-build.
- Dashboard Python compilation: passed.
- Dashboard headless Tk GUI test: six cards and sample data parsing/rendering passed. Sample data was GUI test data only and is **not** hardware evidence.
- Exact hardware command tests, ST-LINK flashing, live STM32 CPU runtime, free heap and speaker playback: pending user board test.
- The device is single-core. Never claim two tasks run at the same instant; FreeRTOS runs one at a time with concurrency/time slicing.
