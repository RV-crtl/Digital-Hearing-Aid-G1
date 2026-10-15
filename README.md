# Group G1 — Digital Hearing Aid (Nucleo integration baseline)

This is a **real STM32CubeIDE importable project** based on the last demonstrated
R34 connectivity firmware. See `Documentation/GROUP_G1_IMPORT_AND_BUILD.md`.

**Working R34 baseline preserved:** Bluetooth/telecoil audio transport,
portable source-selection state machine, speaker DMA, DSP, FreeRTOS tasks,
CPU profiling and the optional 20-second contention experiment.

**New code integrated:** Queued live profile controls, sample-by-sample smooth
gain transitions, WIND low-frequency filtering, bounded automatic loudness
attenuation, Nucleo USER button+LD2, portable quadrature decoder module,
real telemetry `g_group_telemetry`, and independent host C regression tests.

**Not complete:** group custom PCB bring-up, PDM microphone integration,
trained TinyML, phone BLE profile writes, USB audio/charging, beamforming,
physical battery sensing. Those require separate hardware or measured data.
See `Hardware/CUSTOM_PCB_PORTING_CHECKLIST.md` and
`Documentation/GROUP_FEATURE_MANUAL.md`. No medical safety certification.

## Repo layout

- `Core/`, `Drivers/`, `.project`, `.cproject`, `.ioc`: real STM32CubeIDE project
- `PREPARE_PROJECT.bat`/`.ps1`, `Recovery/`: deterministic Windows preparation
- `Dashboard/`: legacy USB debug dashboard (not required on showcase day)
- `References/GroupMemberPrototypes/`: group members' original code snapshots, not compiled
- `Tests/`: portable C audio-profile and rotary-decoder tests
- `Hardware/Original_KiCad/`: supplied PCB schematic/layout reference
- `Hardware/STM32_U13_PINMAP.csv`: real U13 physical pin audit
- `Documentation/`: features, hardware gates, and exact import/flash steps

Build and flash only after PREPARE finishes with PASS; see above docs.

## G2 engineering reliability upgrade (development branch)

- Non-allocating **AudioTask-owned guardian** suspends optional group DSP during genuine audio-DMA overrun/underrun or near-full capture queues, recovering after 3 seconds of clean telemetry. R34 audio transport is not disabled.
- DSP bypass works with both in-place and separate output buffers.
- Host regressions and GitHub Actions CI cover DSP/encoder/guardian and source/recovery integrity.
- See [`Documentation/G2_RELEASE_NOTES.md`](Documentation/G2_RELEASE_NOTES.md) for scope, conditions and limitations.

**Integration status:** Not an ARM-build-verified custom-PCB release, trained TinyML model, battery charger, or safety-certified medical device.
