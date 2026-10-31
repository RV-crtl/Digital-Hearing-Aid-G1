# Digital Hearing Aid

### STM32L476RG · FreeRTOS · physical user controls · event-driven audio · reproducible validation

[![Portable host tests](https://github.com/RV-crtl/Digital-Hearing-Aid-G1/actions/workflows/verify-g3.yml/badge.svg?branch=main)](https://github.com/RV-crtl/Digital-Hearing-Aid-G1/actions/workflows/verify-g3.yml)
![MCU](https://img.shields.io/badge/MCU-STM32L476RG-226699)
![Platform](https://img.shields.io/badge/platform-NUCLEO--L476RG-0f766e)
![Hardware](https://img.shields.io/badge/custom_PCB-not_validated-8b5e3c)
![ML](https://img.shields.io/badge/trained_ML-not_integrated-64748b)

> **Engineering prototype / team project.** The current STM32CubeIDE target is **custom PCB intended for standalone demonstration; NUCLEO-L476RG is the presently source-integrated firmware baseline**, with Bluetooth and telecoil PCM capture, DMA speaker output, a real FreeRTOS task structure, software source selection, optional DSP profiles, and a fault-pressure guardian, DMA-triggered AudioTask wake-ups, and a tested standalone one-button interface. The checked-in custom PCB is a **separate, unvalidated target**. This is not a certified hearing aid or validated acoustic safety device.

![Digital Hearing Aid system overview](Assets/system-overview.svg)

## R20 integration status

The team has separately demonstrated microphone, speaker, Bluetooth, MPR121 touch and rotary-encoder functions in the R17–R20 custom-PCB firmware series. **This repository's legacy CubeIDE application is not yet equivalent to the separate R20 hardware build**; telecoil operation and battery-only cold-start remain open acceptance items. The R20 physical-control policy and quadrature driver are being integrated with a deterministic host regression; see [R20 integration and evidence status](Documentation/R20_HARDWARE_STATUS.md). Do not equate a green host test with a successfully flashed custom-PCB binary.

## G11: standalone target and interactive systems atlas

**Demonstration target:** the physical custom PCB, untethered, powered by **2 × 26650 LiFePO4 cells connected in series (3.25 V nominal per cell, 6.50 V nominal pack)**. PC/ST-LINK and NUCLEO are **not** part of operating mode. The team reports working hardware; its precise board revision and final integrated image still need reproducible on-board tests for this Git SHA. Never route 6.5 V directly to the MCU supply pins: verify actual regulator/protection/balance circuit and safe operating range.

The [visual systems atlas](https://rv-crtl.github.io/Digital-Hearing-Aid-G1/#architecture) explains the complete signal/data/control and power flow, DMA buffer hand-offs, RTOS task priorities, microphone PDM/DFSDM path, Bluetooth/telecoil and optional learning. The [G11 custom-PCB assessment plan](Documentation/G11_STANDALONE_CUSTOM_PCB_RELEASE_PLAN.md) maps these items directly to the external black-box demonstration rubric.

**Implemented vs research:** the microphone's PCB clock/data nets exist but its DFSDM firmware integration is not established; `Core/Src/adaptive_preferences.c` now implements a portable, bounded user-feedback preference algorithm with deterministic host tests, but it is not yet physically wired into MCU capture, control state or nonvolatile memory. The website's learning demo is a simulator, not a claim of deployed hardware AI.

## Choose your route

| Get started | Engineering and evidence | Collaboration |
| :--- | :--- | :--- |
| **[Import into STM32CubeIDE](Documentation/GROUP_G1_IMPORT_AND_BUILD.md)** | **[Find a function in the code](Documentation/CODE_MAP.md)** | **[Contribution standards](CONTRIBUTING.md)** |
| **[Standalone controls and LED guide](Documentation/G5_STANDALONE_USER_GUIDE.md)** · [Showcase plan](Documentation/STANDALONE_SHOWCASE.md) | [Run host/MATLAB verification](Validation/README.md) | [Open issues](https://github.com/RV-crtl/Digital-Hearing-Aid-G1/issues) |
| [Architecture](Documentation/ARCHITECTURE.md) · [RTOS/CPU measurements](Documentation/G5_RTOS_AND_PERFORMANCE.md) | [Evidence and hardware gates](Documentation/VALIDATION_LADDER.md) | [Change history](CHANGELOG.md) |
| [Hardware/PCB design](Hardware/README.md) | [Requirement-by-requirement matrix](Documentation/REQUIREMENTS_EVIDENCE.csv) | [Pull requests](https://github.com/RV-crtl/Digital-Hearing-Aid-G1/pulls) |

**New contributor?** The [one-page repository guide](Documentation/REPOSITORY_GUIDE.md) explains the directory roles, build sequence, source ownership, and how to find the tested code.

## Publication and hardware-release status

**This repository is currently public; a project-wide open-source licence has not been confirmed.** Public visibility does not by itself grant reuse rights. G1 is a student/team **NUCLEO-L476RG engineering prototype**, not a finished commercial/clinical hearing aid. Green CI verifies host source checks only; it cannot establish acoustic safety, enclosure reliability, medical conformity, supply/battery protection or custom-PCB functionality.

Before opening the repository to the public, clear contributor/institution rights and supplied third-party files. See the [public-source and device release gate](Documentation/PUBLICATION_GATE.md), [third-party provenance](Documentation/THIRD_PARTY_PROVENANCE.md), [security disclosure process](SECURITY.md) and [physical release checklist](Documentation/RELEASE_CHECKLIST.md).

`python3 Tools/publication_audit.py --output Validation/results/publication_audit.json` checks the *tracked source tree* for certain accidental credentials, generated files and vendor notices; it **does not scan all git history** or grant publication permission. Raw source, hardware and assessment files remain subject to manual review. The repository must not be represented as medical or acoustic-safety validated.

## No-laptop Nucleo demonstration — physical B1 controls

| Nucleo B1 gesture | Product-facing action |
| --- | --- |
| **1 click** | Cycle listening profile: NEUTRAL → VOICE → MUSIC → WIND |
| **2 clicks** | Toggle **automatic loudness heuristic** (not trained AI) |
| **3 clicks** | Request real source FSM mode: AUTO → Bluetooth → telecoil |
| **Hold 1.2 seconds** | Toggle optional group processing |
| **Hold 4+ seconds** | Restore neutral profile and AUTO routing |

**Nucleo LD2:** off = optional DSP disabled; steady = manual profile; one short blink per second = auto-level; rapid flash = overload guardian. B1/LD2 use PC13/PA5 on the Nucleo; **those signals are not connected as user controls on the supplied custom PCB**.

The [full standalone guide](Documentation/G5_STANDALONE_USER_GUIDE.md) explains debounce, compatibility, shortcuts, and limitations. All gestures are queued through FreeRTOS and consumed by AudioTask rather than changing PCM state inside a GPIO interrupt.

## What actually works, and what is still engineering work?

| Subsystem | Current code | Evidence to date |
| --- | --- | --- |
| Bluetooth and telecoil digital capture and source FSM | Present | Portable FSM regression; original Nucleo playback needs final-assembly retest |
| FreeRTOS / bounded control queue / DMA task notifications / runtime profiling | Present | Static ISR-priority/ownership checks; real RTOS latency requires physical verification |
| SAI/DMA output and PCM routing | Present | Implementation exists; speaker and transducer must be retested |
| HPF / compensation EQ / dynamics | Present | Production-C impulse vectors; independent Python mathematical verification; MATLAB script supplied |
| G1 listening profiles, WIND filter and auto loudness *heuristic* | Present | Host C tests, including identity bypass and transitions; **not trained AI** |
| Overload guardian | Present | Host regression verifies state policy; real DMA overload proof outstanding |
| Nucleo B1 source/profile gestures and LD2 status | Integrated in G5 | Portable debounce/multiclick tests pass; on-board testing pending |
| Physical rotary and PDM mic integration | Partial prototypes | Custom board hardware and firmware integration pending |
| TinyML, USB audio, charging/balancing, production power and custom PCB firmware | **Not implemented / verified** | Requirements and hardware feasibility documented |

## Interactive visual website / GitHub Pages

**G1 / Inside the Signal** is a responsive engineering showcase in [`docs/`](docs/README.md). It includes a WebGL model viewer, functional Bluetooth/telecoil signal map, filterable KiCad schematic gallery with native SVG plotting during deployment, and a browser of the actual embedded C source.

**Launch instructions:** [GitHub Pages setup and CAD upload](docs/README.md). Expected public address **after activation**: https://rv-crtl.github.io/Digital-Hearing-Aid-G1/ . The website is a demonstration interface—not a live data feed, product certification, or independently validated custom-PCB design.

## Repository-wide folder integrity

G9 adds whole-repository Markdown link and Windows filename-collision checks, compilation of every HAL-independent application C module under GCC/Clang, validation of evidence ZIP contents against SHA-256, and strict local ARM report provenance/freshness checks. See [G9 repository audit](Documentation/G9_REPOSITORY_INTEGRITY_AUDIT.md). These automated checks do **not** resolve the externally missing KiCad libraries, build an ARM binary in GitHub CI or certify device safety.

## One-command G1 quality gate

On Windows, run **powershell -ExecutionPolicy Bypass -File .\G1_RUN_ALL.ps1** from the G1 repository root to audit code, requirements and evidence, exercise portable host tests, generate draft assessment PDFs and create a SHA-256-manifested ZIP. Append **-BuildArm** to additionally run G1_BUILD_ARM.ps1 (requires local STM32CubeIDE and vendor firmware). Read the [G7 one-command engineering guide](Documentation/G7_ONE_COMMAND_QUALITY_GATE.md) for dependency requirements and exactly what remains unverified. The operating firmware still requires a separate CubeIDE ARM build and physical tests.

## Fast validation and performance smoke test

From repository root on a computer with GCC/Clang, Python 3 and Bash:

```bash
bash Validation/run_host_validation.sh
```

This builds and executes **production-source C tests**, exports filter vectors, checks the math independently, tests the source-selection FSM, encoder, button gestures and overload guardian, audits KiCad dependencies, and writes a **host-only 128-frame DSP benchmark** to `Validation/results/profile_host_benchmark.csv`.

For MATLAB, after running the host script:

```matlab
addpath('Validation/matlab');
verify_audio_filters('Validation/results/audio_vectors.csv','Validation/results');
```

See the [validation ladder](Documentation/VALIDATION_LADDER.md) for exactly why **host CI does not prove successful ARM compilation, safe sound output, real PCB power behaviour, or medical performance**.

## Repo layout

```text
Core/            Real application, MCU/SAI/DMA and FreeRTOS (compiled)
Drivers/         STM32 HAL and CMSIS vendor dependencies
Recovery/        Copies restored after STM32CubeMX generation
Tests/           Portable C tests against production modules
Validation/      C vectors, independent Python/MATLAB, host benchmarks
Hardware/        Original supplied KiCad project and MCU pin map
Documentation/   Architecture, code map, tutorials, requirements and gates
References/      Group members' earlier prototypes (NOT compiled)
Dashboard/       Optional laptop-based development tooling
.github/         Automated checks and contribution/review workflows
```

**Keep the CubeIDE `.project`, `.cproject`, `.ioc` and prepare scripts at the repository root.** `Recovery/` is not a second build target. See [exact import/build instructions](Documentation/GROUP_G1_IMPORT_AND_BUILD.md) before opening the `.ioc`.

## Software design and priorities

- **AudioTask (priority 7):** exclusive owner of DSP and PCM audio control state. Queued user commands enter at safe task boundaries.
- **ControlTask (priority 6):** controls/serial processing and diagnostic snapshots; should not block critical audio work.
- **Bluetooth supervision (priority 5):** independent status handling.
- **Background tests (priority 2):** development-only workloads. Deliberate priority-7 saturation is **not normal product operation**.
- **Engineering goal:** use DMA and event/queue synchronization while measuring real worst-case timing—not merely adding more tasks.

The [code map](Documentation/CODE_MAP.md) takes you directly to the implementation symbols and explains which code can be safely changed without touching the verified TIM2/TIM6 runtime profiler.

## Hardware truth / release status

The supplied routed KiCad design is included for inspection and migration. The board has unresolved third-party schematic/footprint-library paths, outstanding KiCad ERC/DRC, unverified user-control wiring, and incomplete evidence for battery charging/balancing. The STM32 custom-board pin mapping **differs from the Nucleo firmware**.

Read [hardware constraints](Hardware/README.md), [board-porting checklist](Hardware/CUSTOM_PCB_PORTING_CHECKLIST.md) and [release gates](Documentation/RELEASE_CHECKLIST.md) before custom-board programming or a product-level performance claim.

---

**Team engineering repository.** Please submit changes via a branch + pull request, preserve test evidence by commit, and keep the original individual PD5 repository separate. Every claimed hardware PASS requires recorded conditions and measured results.

---

## 🎓 ZEIT4232 System Demonstration & Viva — HD readiness

> The supplied final rubric requires **all revised whole-system requirements** to be convincingly demonstrated before any advanced/HD extension can count. The demonstration is **black-box and physically observable**, runs **without ST-LINK or serial debugging**, and must complete in **10–20 minutes** where feasible (30-minute hard limit).

| Start here | Purpose |
| --- | --- |
| **[HD rubric crosswalk](Documentation/HD_RUBRIC_READINESS.md)** | Explicit minimum → meets → exceeds gates |
| **[Black-box 20-minute test plan](Documentation/BLACK_BOX_TEST_PLAN.md)** | Technician-ready test procedures with prerequisites / expected results |
| **[System-level requirements coverage](Documentation/HD_REQUIREMENTS_COVERAGE.md)** | Requirement IDs, observable tests and unresolved blockers |
| **[A3 hardware/software diagram](Documentation/PRINTABLE_SYSTEM_DIAGRAM.md)** | Print **two** labelled copies |
| **[Mechanical HD acceptance](Documentation/HD_MECHANICAL_ACCEPTANCE.md)** | Case, connectors, buttons, ergonomics, fit and finish |
| **[Individual viva technical map](Documentation/VIVA_INTERFACE_MAP.md)** | Tasks, DMA, queue contracts, faults, code ownership and troubleshooting |
| **[Release gate](Documentation/HD_RELEASE_GATE.md)** | Mandatory verification before calling this operating system ready |
| [Blank physical test run record](Documentation/BLACK_BOX_RESULTS_TEMPLATE.md) | External observations, instrument data and sign-off |

**Important:** The original matrix still contains unintegrated features (mic, feedback, wear/power) and unmeasured limits. All tests are intentionally **NOT_RUN/BLOCKED until physically confirmed**. A green GitHub CI check does not satisfy black-box hardware validation. The draft plan must be reconciled and revised by the group before the **single combined requirements + test-plan PDF** is submitted to Moodle.
