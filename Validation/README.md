# Verification and validation — start here

**No invented pass marks.** Each method indicates exactly what it does and what it cannot prove. The current source tree is a Nucleo/FreeRTOS integration baseline, not a medical device or fully validated custom-board firmware.

## One-command host regression (Windows: Git Bash/WSL; Linux/macOS)

From repository root:

```bash
bash Validation/run_host_validation.sh
```

Needs Python 3, GCC and a POSIX shell. This compiles *actual production C sources*, runs portable unit/scenario tests, generates `Validation/results/audio_vectors.csv`, independently checks their floating-point filter mathematics, validates project/recovery guards, and runs a non-destructive KiCad source dependency audit. Generated binaries/CSV/plots belong under ignored `Validation/results/`.

## MATLAB filter validation (no specialist toolbox)

After host regression, start MATLAB in the repository root and run:

```matlab
metrics = verify_audio_filters('Validation/results/audio_vectors.csv', 'Validation/results');
```

Add `Validation/matlab` to the MATLAB path first, or `cd Validation/matlab` and supply absolute paths. It produces `impulse_hpf.png`, `impulse_hpf_eq.png`, and `filter_frequency_response.png`, using the *exact coefficient values in* `Core/Src/audio_dynamics.c`. It compares simulated doubles to the actual compiled C block processor output (small PCM quantisation tolerance). It does **not** establish acoustically measured frequency response, THD, feedback rejection, SPL or actual hearing benefit.

## What is covered

| Subsystem | Reproducible software evidence | Physical acceptance remaining |
| --- | --- | --- |
| Audio HPF / compensation EQ | Compiled C impulse → Python and MATLAB independent biquad response | Inject tone and measure analogue/output response with calibrated load |
| Group gain/WIND profile | C host tests and source guards | Volume quality, transitions and numerical benchmark on Cortex-M4F |
| Audio overload guardian | C tests: error count, queue threshold, recovery and tick wrap | Real DMA load and recoverability without new glitches |
| Bluetooth/telecoil controller | FSM scenario tests using production portable controller | Real link-loss, reacquisition and source switching |
| Rotary encoder | C quadrature test and glitch case | Physical PA0/PA1 wiring and debouncing on fabricated board |
| FreeRTOS | Source guards for scheduler, priorities, queue and timer continuity | Task list, runtime%, stack high-water, timing/jitter on board |
| USB-C / power | KiCad completeness audit, design checklists | Electrical bring-up, DRC/ERC, USB enumeration, charger safety |
| PDM mic / ML | Source hardware inventory only | DFSDM verified input and a genuinely trained/evaluated ML model |

## Proof standard

**Unit verification** establishes a module implements its specified algorithm. **Integration verification** requires observing signals/events through interacting subsystems. **Validation** demonstrates intended behaviour against product-level requirements on real hardware. For any claim of PASS, archive test conditions, measured values, hardware revision, firmware commit, calibration, pass limits and raw evidence. Host-only checks cannot certify the integrated hearing aid.
