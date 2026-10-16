# Changelog

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
