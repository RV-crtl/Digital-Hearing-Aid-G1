# Digital Hearing Aid — STM32L476RG / FreeRTOS

**Group integration engineering repository · G3 candidate, hardware validation pending**

> A multi-input bone-conduction hearing-aid prototype with Bluetooth/telecoil audio, SAI/DMA playback, portable source-selection state machine, fixed-memory DSP profiles, overload guardian, event-queue user controls, and RTOS CPU profiling. The checked-in CubeIDE target is **NUCLEO-L476RG**, **not** custom-PCB-ready or a certified medical/hearing protection device.

## Start in 60 seconds

| I want to… | Open |
| --- | --- |
| See the actual hardware/software architecture | [Architecture guide](Documentation/ARCHITECTURE.md) |
| Import, build and flash the current Nucleo target | [Exact STM32CubeIDE steps](Documentation/GROUP_G1_IMPORT_AND_BUILD.md) |
| Know which functions are finished / hardware-dependent | [Requirements–evidence matrix](Documentation/REQUIREMENTS_EVIDENCE.csv) |
| Run verification without the STM32 board | [Host tests + MATLAB figures](Validation/README.md) |
| Operate the hearing aid without a laptop | [Standalone demonstration](Documentation/STANDALONE_SHOWCASE.md) |
| Inspect the full supplied KiCad schematic/PCB | [Hardware navigation and dependency audit](Hardware/README.md) |
| Understand the hardware porting blockers | [Custom PCB gate](Hardware/CUSTOM_PCB_PORTING_CHECKLIST.md) |
| Safely collaborate / submit changes | [Contributing](CONTRIBUTING.md) |
| Decide whether a version is release-ready | [Release checklist](Documentation/RELEASE_CHECKLIST.md) |

## Available NOW vs proposed

| Capability | Present software | Evidence level |
| --- | --- | --- |
| Bluetooth and telecoil capture/routing + bone-conduction output | Existing R34/G1 code | Host/source + previous Nucleo tests; retest per assembled unit |
| FreeRTOS task architecture, control queue and CPU runtime instrumentation | Implemented | Source checks; real timing/CPU need board evidence |
| Source-selection M1–M4 policy with fault and recovery | Implemented | Portable production-C scenario tests |
| 150 Hz HPF, 1.6 kHz compensation EQ, gradual dynamics | Implemented | Production-C golden samples, independent Python + MATLAB comparisons |
| Four optional listening profiles, WIND attenuation | Implemented | Portable C regressions |
| DSP overload guardian | Implemented | Host tests; real DMA overload acceptance outstanding |
| Nucleo USER button profile control | Implemented | Physical validation outstanding |
| KiCad project and routed PCB | Source assets included | **50 unresolved external library URIs**; ERC/DRC and assembly not verified |
| PDM microphone, TinyML, Bluetooth phone configuration, USB audio/charging, custom PCB firmware | **Not integrated/verified** | Separate hardware/firmware work required |

## Run the portable tests

```bash
bash Validation/run_host_validation.sh
```

Requires Python 3, GCC and Bash (Linux/macOS or Windows Git Bash/WSL). The script builds real application C source modules, runs controller/DSP/encoder/guardian regression scenarios, produces a filter impulse CSV, checks mathematical responses and audits KiCad project dependencies. GitHub Actions runs the same suite automatically; green host CI is **not** a green hardware-validation badge.

In MATLAB, add `Validation/matlab` to path, generate `Validation/results/audio_vectors.csv` using the command above, then call:

```matlab
verify_audio_filters('Validation/results/audio_vectors.csv','Validation/results');
```

## Repository design

- **Root**: CubeIDE `.project`, `.cproject`, `.ioc`, preparation scripts; do not relocate these.
- **Core / Drivers / Recovery**: Embedded app and deterministic recovery mirrors. Regenerating CubeMX can replace non-USER sections: see import guide.
- **Tests / Validation**: Portable C tests, C reference vectors, independent analysis and generated evidence (ignored).
- **Hardware / Original_KiCad**: Group-provided electrical design and MCU pad audit; third-party library dependencies must be restored for editing.
- **Documentation**: product architecture, hardware gates, requirement evidence and on-device demo.
- **References**: Group-member prototype snapshots; not compiled into the integrated firmware.
- **Dashboard**: Development aid only; not required for standalone showcase.

## Version discipline

`main` remains the group's recoverable baseline. Development occurs in pull requests and requires host regressions plus ARM build and physical evidence before declaring any integrated release complete. The original individual PD5 repository is **not** a dependency and must not be modified by this project.

**No calibrated SPL limit, medical device certification, production battery charger or complete PCB hardware-in-the-loop verification is claimed.**
