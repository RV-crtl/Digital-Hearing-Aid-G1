# G1 · Standalone adaptive hearing prototype

**STM32L476RG · custom KiCad PCB · FreeRTOS · microphone / Bluetooth / induction-loop audio · physical controls**

![Actual G1 signal architecture](Assets/system-overview.svg)

The diagram above represents real interfaces and task ownership, not fictional mechanical CAD or hardware measurements.

[![Engineering CI](https://github.com/RV-crtl/Digital-Hearing-Aid-G1/actions/workflows/verify-g3.yml/badge.svg)](https://github.com/RV-crtl/Digital-Hearing-Aid-G1/actions/workflows/verify-g3.yml) [![R26 DSP tests](https://github.com/RV-crtl/Digital-Hearing-Aid-G1/actions/workflows/r26-dsp.yml/badge.svg)](https://github.com/RV-crtl/Digital-Hearing-Aid-G1/actions)

[**Explore the interactive website**](https://rv-crtl.github.io/Digital-Hearing-Aid-G1/) · [System architecture](Documentation/PRINTABLE_SYSTEM_DIAGRAM.md) · [Black-box demonstration plan](Documentation/BLACK_BOX_TEST_PLAN.md) · [Current evidence and requirements](Documentation/G1_CURRENT_SYSTEM_REQUIREMENTS.md) · [Hardware/KiCad](Hardware/README.md)

> **Prototype, not a certified hearing aid.** This repository contains original PCB design, software source, testing infrastructure and student-team engineering work. The maximum acoustic output has not been independently calibrated for use on or in the ear. Public access is not a general reuse licence; see [publication and contributor rights](Documentation/PUBLICATION_GATE.md).

## Current firmware — QR261104.8 (single canonical CubeIDE project)

**Source of truth:** the repository root now contains the promoted custom-PCB R26 project, with exactly one `Core/Src/main.c`, one `261104-Digital_Hearing_Aid.ioc`, and no `Recovery/` mirror. [Browse active source](Core/Src) · [Step-by-step boot-to-speaker function walkthrough](Documentation/FIRMWARE_EXECUTION_WALKTHROUGH.md) · [Quick code map](Documentation/CODE_MAP.md).

**Import from GitHub:** choose **Code → Download ZIP**; fully extract; in STM32CubeIDE select **File → Import → Existing Projects into Workspace**, browse to the extracted repository root, then import **261104-Digital_Hearing_Aid**. Close/remove any older project with the same workspace name first. Use **Project → Clean**, then **Build Project**. Inspect the newly generated ELF, compiler/linker warnings and physical PCB setup before flashing. No Python/Markdown files are compiled: they are GitHub repository documentation, tests and site infrastructure. For the minimal 246-file CubeIDE-only archive, use the companion QR261104.8 CLEAN project ZIP.

**Configuration warning:** custom DFSDM, DMA, clock and RTOS startup were hand-integrated. CubeMX **Generate Code** can overwrite these modifications; make a commit before regeneration and check the source diff. This branch updates the firmware source, but a fresh native CubeIDE GCC build and board-level telecoil listening/SNR tests are still required.

## The real prototype

The physical target is a **standalone custom PCB**, not a Nucleo board. A ST-LINK is needed only for programming and optional engineering diagnosis, not for intended normal operation. The hardware incorporates an STM32L476RG, digital PDM microphone, MPR121 capacitive touch, rotary encoder, Bluetooth audio, a telecoil input with PCM1808 conversion, FreeRTOS and a speaker/audio-output chain. The bench-tested R24 firmware was reported to operate without Nucleo after a wiring issue was resolved.

The team subsequently developed **R26** with higher-sensitivity ambient audio, telecoil idle-noise reduction, adaptive gain, and incremental acoustic feature clustering. **R26 firmware is a candidate, not a confirmed on-PCB acceptance result.**

| Function | Evidence currently available | Important qualification |
| --- | --- | --- |
| Microphone → speaker | Operator-confirmed in R24; R26 DSP host tests | Distant pickup, distortion, safe SPL and ambient balance need measurement |
| Bluetooth → speaker | Operator-confirmed in R24 | Bluetooth module and source output remain subject to demonstration checks |
| Telecoil → speaker | QR261104.8 LOW24 recovery and filtered idle processing; prior board listening | Residual analogue hiss/weak-signal behaviour needs listening and bench measurements |
| MPR121 touch / encoder | Operator-confirmed, R21 control regression tests | Repeatability and user-response timing remain black-box criteria |
| Untethered USB-C power | R24 operator-confirmed after resolving external wiring | Battery source/protection/charging cannot be inferred from this result |
| Adaptive scene model | R26 portable C tests | **Three unsupervised clusters**, not a pretrained speech/car/pedestrian classifier |
| FreeRTOS / DMA | Static contracts and host tests | Final CPU load, timing, interrupts, underruns and SRAM headroom need recorded board evidence |

## Firmware revision and evidence boundary

The QR261104.8 project supersedes the prior root-level Nucleo/group demonstration source. Bluetooth/telecoil PCM1808 capture, DFSDM microphone, local controls, FreeRTOS audio tasks and MAX98357 output are in one importable root project. The telecoil now includes fixed 3.4-kHz speech-band conditioning plus weak-signal-aware idle attenuation; **noise during an active weak programme remains a hardware/measurement question**. The older integration issue [#27](https://github.com/RV-crtl/Digital-Hearing-Aid-G1/issues/27) is historical context, not a request to combine different firmware trees manually.

## Interactive engineering site

The [site](https://rv-crtl.github.io/Digital-Hearing-Aid-G1/) provides an interactive CAD explorer (3MF/GLB with local upload), source-linked **original KiCad schematics**, signal paths and task ownership, code references, and a demonstration checklist. All files under `Hardware/Original_KiCad/` remain unchanged.

The uploaded **November 2026 full multi-body assembly** contains 1,713 3MF mesh objects (460,021 triangles), including PCB, housing and separated structures. The *previous* 3MF stored at `docs/assets/g1-assembly.3mf` is an earlier revision. The newest upload can be opened through the site's **Open your 3D CAD** control without sending it to a server; its full binary is not yet tracked on GitHub. See [CAD provenance and viewing guide](Documentation/CAD_ASSEMBLY_AND_VIEWERS.md).

## Project navigation

| Area | Start here |
| --- | --- |
| Software / RTOS | [Active Core source](Core/Src) · [power-on-to-speaker walkthrough](Documentation/FIRMWARE_EXECUTION_WALKTHROUGH.md) · [source map](Documentation/CODE_MAP.md) |
| DSP and adaptive learning | [Ambient DSP](Core/Src/g1_ambient_dsp.c) · [online clusters](Core/Src/g1_scene_ml.c) · [telecoil cleanup](Core/Src/tc_cleanup.c) |
| Circuits / layout | [Original KiCad files](Hardware/Original_KiCad) · [hardware notes](Hardware/README.md) |
| Firmware import | [Quick import procedure above](#current-firmware--qr2611048-single-canonical-cubeide-project) · [function walkthrough](Documentation/FIRMWARE_EXECUTION_WALKTHROUGH.md) |
| Demonstration | [System diagram](Documentation/PRINTABLE_SYSTEM_DIAGRAM.md) · [current requirements](Documentation/G1_CURRENT_SYSTEM_REQUIREMENTS.md) · [20-minute black-box test](Documentation/BLACK_BOX_TEST_PLAN.md) |
| Validation / CI | [Host validation](Validation/README.md) · [Actions](https://github.com/RV-crtl/Digital-Hearing-Aid-G1/actions) |
| Contributors / release | [CONTRIBUTING](CONTRIBUTING.md) · [SECURITY](SECURITY.md) · [publication gate](Documentation/PUBLICATION_GATE.md) |

## Working practices

Run `python3 Validation/run_host_validation.py`, `python3 docs/validate_site.py` and the relevant portable C tests before submitting a PR. **Green source-level CI does not mean ARM firmware compiled, a device booted, or acoustic performance was measured.** The correct demonstration image must start from its intended regulated power supply with ST-LINK and diagnostics disconnected; no synthetic stress/demo tasks should be enabled for operating mode.

Earlier G2/G3/G5/G11 and R-series materials remain in `Documentation/` for traceability, but **the current requirements and black-box plan linked above supersede their historical demonstration descriptions.** Avoid presenting historical statements about an unintegrated mic, Nucleo B1, or simulated user controls as the current PCB's features.
