# Changelog

## G8 — public-source engineering hardening (20 October 2026; publication and physical release **blocked**)

- Added portable C boundary tests for command-frame parsing and S24 PCM decode, stereo/mono routing, gain/mute/saturation, now executed under GCC, Clang and address/UB sanitizers.
- Added tracked-source credential/build-artifact hygiene audit, corruption tests and a human-rights clearance gate; **a passing audit is NOT permission to make the repository public**.
- Moved dashboard result pointers into ignored local results and isolated Windows dashboard dependencies in `Dashboard/.venv`; improved output path regression checks.
- Reduced redundant host compilations in CI while retaining requirement traceability checks, source/mirror preflight and numerical evidence.
- Added third-party provenance inventory for ST HAL, CMSIS, group PCB, student prototypes and assessment PDFs.
- No claim of ARM flash success, calibrated acoustic safety, custom-PCB verification, clinical suitability or redistribution rights. GitHub repository remains private until rightsholders approve.

## G5 — 17 October 2026 (staged, hardware validation pending)

- DMA completion now wakes AudioTask using FreeRTOS task notifications after ring-buffer publication; 5-ms timeout services FSM when idle. TIM2 runtime profiler, FreeRTOS priorities and interrupt configuration are retained.
- New portable Nucleo B1 single/double/triple/long/reset gesture decoder controls profiles and real source requests through the existing FreeRTOS queue.
- Nucleo LD2 communicates manual, automatic and guardian-bypass states. These controls are *not* custom-PCB features.
- Additional deterministic portable gesture regression, static ISR-to-task priority/ownership contract and GCC/Clang + sanitizer CI.
- Explicit hardware-measurement protocol, standalone help and known limitations.


## G4 — maintainable GitHub project and DSP performance (host-verified candidate)

- Replaced legacy first-run note with product navigation and published a system architecture SVG.
- Added a code map, source ownership guide, evidence ladder and automated documentation/integrity checks.
- Optimised unity-gain G1 DSP to avoid redundant per-sample arithmetic while retaining scene observer/counters.
- Added a portable fast-path regression (8/8) and host benchmark CSV; no Cortex-M4 timing claim.
- Added GCC/Clang verification matrix and sanitizer checks, with GitHub artifacts.
- Documented FreeRTOS task priorities, single-writer ownership and the production prohibition on the priority-7 spin task.
- Captured remaining ARM/PCB/microphone/power/ML acceptance work in GitHub issues.
- **Full ARM cross-build, fabricated PCB, USB battery charging and acoustic safety are not verified.**

## G3 — documentation and reproducible verification (candidate)

- Navigator README and human-oriented architecture, standalone demo, requirements/evidence and release criteria.
- Actual production C DSP impulse vectors, independent Python filter response checks and MATLAB plots.
- Portable source-policy regression for connectivity M1–M4 scenarios.
- Non-destructive KiCad project completeness audit, with unresolved vendor libraries explicitly flagged.
- Expanded continuous integration and issue/PR contribution workflow.

## G2 — current development baseline

- Optional-DSP overload guardian and recovery, correctness fix for bypassed buffers, enhanced CI and C host tests. See `Documentation/G2_RELEASE_NOTES.md`.

## G1 — integrated Nucleo development baseline

- R34 connectivity audio/RTOS foundation, G1 audio profiles, queued control events, Nucleo USER button and portable rotary decoder. Custom board still pending.
