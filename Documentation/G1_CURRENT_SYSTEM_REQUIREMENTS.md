# Current G1 system requirements and release evidence

**As of 03 November 2026.** Canonical current-system interpretation for the custom PCB. Prior G-series Nucleo requirements under `Documentation/` are historical rather than claims about the flashed product.

## System-level scope

| Requirement | R24 operator report | R26 evidence | Unresolved gate |
| --- | --- | --- | --- |
| Independent startup | R24 worked after external wiring correction; Nucleo no longer required in that test | Source configurations intended for standalone | Independently record repeated cold boots on intended power source |
| Microphone pickup | Audible and functional, sensitivity still under development | 20× bounded DSP host tests; weak-signal compensation | Record environmental pickup, distortion, acoustic output and background noise |
| Bluetooth audio | Functional pairing and playback | Transport retained from R24 | Link reconnection and source-priority tests |
| Telecoil playback | **LOW16 decode worked; FILTER ON helped** | Adaptive gain and low-level idle filtering host tests | Quantify idle hiss, wanted signal retention and noise under real fixture |
| Physical UI | E8 BT / E9 TC / E10 mute / E11 MIC; encoder | Debounce and volume regression tests | ≥5 intentional hits per electrode and 5-step encoder response |
| Classification and adaptation | Earlier contextual preference logic | 3-centroid **unsupervised** feature clustering; bounded feedback AGC | Real signal classification stability and DSP CPU utilisation |
| Safe output | Digital limiter only | Bounded algorithm checks | **Acoustic SPL and electroacoustic limiter are NOT verified** |
| RTOS quality | Working DMA and task prototype | Source and host checks | Instrument deadline, CPU, stack, underruns and resilience on target |

## Firmware origin and safe integration

- **R24:** operator-confirmed baseline for USB-C independent operation, mic/BT/TC, touch and rotary. Keep as a rollback.
- **R26:** later firmware candidate with `g1_ambient_dsp.c`, `g1_scene_ml.c`, `tc_cleanup.c`, bounded preference learning and existing peripheral drivers. Its ARM object tests passed but this is not a full native GCC-flashed validation record.
- **Repository root:** legacy Nucleo-oriented STM32CubeIDE source plus selectively integrated DSP/control modules. It **is not equivalent** to the whole R26 custom-PCB firmware yet. Do not represent the root `.ioc` as a matching board pinout.
- **Original KiCad:** unchanged. Source schematic layout is preserved; external library resolution and ERC/DRC require separate validation.
- **CAD:** the previous 3MF is tracked on GitHub. The new user-supplied, all-bodies assembly is a distinct 6.47 MB 3MF with 1,713 meshes / 460,021 triangles; it is not yet published in the GitHub tree. The site's local importer can open it.

## Candidate performance targets (not verified results)

The demonstration plan proposes 3 cold starts, 5/5 deliberate touches per electrode, audible speech at 0.5 and 2–3 metres under noted room conditions, reproducible induction-loop source selection with idle rejection, and no unexpected crashes. These are testable goals, **not** project results or formal medical acoustic requirements.

## Required release gates

1. Whole R26 or successor project cleanly compiles/links with STM32CubeIDE 1.19.0 and records the real `.elf`, `.map`, compiler/HAL/FreeRTOS versions and commit.
2. Runs standalone on intended physical supply without ST-LINK, PC serial or synthetic load tasks, while using the real original PCB.
3. Passes independent black-box plan, including user inputs in unexpected orders, source interruptions and repeated power cycles.
4. Measures peak output on a fixture and establishes an acceptable bound **before** any listening test involving ears.
5. Confirms the real CAD, KiCad and team artefact copyright/permission status before redistribution. Source scans are not a substitute for rights review.

See [test plan](BLACK_BOX_TEST_PLAN.md), [printable system architecture](PRINTABLE_SYSTEM_DIAGRAM.md), and [integration issue](https://github.com/RV-crtl/Digital-Hearing-Aid-G1/issues/27).
