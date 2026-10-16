# Engineering evidence ladder — verification is not validation

The firmware is for an engineering prototype, **not a certified clinical hearing aid**. Every positive claim needs a measurement method and an auditable record. A green GitHub check means host tests passed, not that the transducer is safe.

| Level | Evidence | Run here | Gating status |
| --- | --- | --- | --- |
| V0 — Static | CubeIDE metadata, recovery mirrors, source guards, link integrity | `python3 CHECK_PROJECT.py`, `python3 CHECK_GROUP.py`, `python3 Validation/python/check_repository.py` | Host automated |
| V1 — Component | DSP exact samples and filter math, FSM events, encoder, overload governor | `bash Validation/run_host_validation.sh` | Host automated |
| V2 — Numeric independent | MATLAB compares compiled C impulse vectors to double-precision biquad model | `Validation/matlab/verify_audio_filters.m` | MATLAB run required; not CI-generated MATLAB evidence |
| V3 — Integration | CubeIDE **Debug build** and flash; source selection, DMA overflows, RF/inductive paths, FreeRTOS task timing | `Documentation/RELEASE_CHECKLIST.md` | **Not automated; not complete** |
| V4 — Custom PCB | KiCad ERC/DRC, real measured rails and current, pin/peripheral validation, audio paths, USB VBUS/CC and ESD | `Hardware/README.md` | **Not complete** |
| V5 — User outcome | Calibrated latency/THD/SNR/output level, device usability, mechanical enclosure and battery safety | Lab measurement protocol and signed report | **Not complete** |

## Real audio performance record template

Record **hardware revision**, assembly variants/jumpers, firmware commit, clock, sample rate, ADC and output route, test stimulus amplitude/frequency, transducer or dummy load, supply voltage, instrument calibration, measured value and acceptance criterion. Attach the raw CSV/scope traces and date. Do not substitute a MATLAB simulation for physical acoustic level or THD.

## Reproducing CPU optimisations

Run `bash Validation/run_host_validation.sh`. It executes deterministic output tests and emits `Validation/results/profile_host_benchmark.csv`. The CSV contains **host nanoseconds per 128-frame block** for different optional DSP profiles. It is a reproducible performance *smoke measurement*, not MCU CPU utilisation or a guaranteed speedup: compare the same machine, compiler, optimisation options and multiple trials; benchmark on an STM32 using TIM2 measurements before claiming real-time gains.

## Fail-safe policy

The G2 Guardian observes buffer depth and existing error counters and temporarily disables only optional G1 processing. It does not establish an acoustic safety limit, prevent all deadline misses, or replace a hardware watchdog. High-priority busy-loop stress remains a **development-only diagnostic**.
