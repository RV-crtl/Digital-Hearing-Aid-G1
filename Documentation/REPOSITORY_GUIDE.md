# Repository navigation — find what you need in two clicks

| Role | First document | Where to make changes | What to run |
| --- | --- | --- | --- |
| New group member | [Code map](CODE_MAP.md) | [Core application](../Core/Src/) | `bash Validation/run_host_validation.sh` |
| Audio/DSP developer | [Verification ladder](VALIDATION_LADDER.md) | [`audio_dynamics.c`](../Core/Src/audio_dynamics.c), [`group_features.c`](../Core/Src/group_features.c) | C vectors + MATLAB |
| Connectivity developer | [Architecture](ARCHITECTURE.md) | [`connectivity_controller.c`](../Core/Src/connectivity_controller.c) | FSM scenarios |
| RTOS developer | [Code map](CODE_MAP.md) | [`demo_app.c`](../Core/Src/demo_app.c), [`rtos_bootstrap.c`](../Core/Src/rtos_bootstrap.c) | Host guards + board profiler |
| PCB/power developer | [KiCad guide](../Hardware/README.md) | [Original KiCad design](../Hardware/Original_KiCad/) | dependency audit, KiCad ERC/DRC, bench |
| Group-demo presenter | [Standalone showcase](STANDALONE_SHOWCASE.md) | physical prototype + status evidence | demo rehearsal |
| Maintainer/reviewer | [Contribution guide](../CONTRIBUTING.md) | Pull requests, [requirements matrix](REQUIREMENTS_EVIDENCE.csv) | all CI + on-board signoff |

## STM32CubeIDE first import

1. Clone/download the branch in a path without spaces if possible.
2. Use **File → Import → General → Existing Projects into Workspace** at the repository root. Keep *Copy projects into workspace* unchecked.
3. Read the detailed [build procedure](GROUP_G1_IMPORT_AND_BUILD.md). If CubeMX code generation is necessary, do it **before** running `PREPARE_PROJECT.bat` with CubeIDE closed.
4. Reopen IDE, refresh, clean and build. **Full ARM build and hardware flash need confirmation**; passing host CI does not imply an ELF was compiled on GitHub.
5. Initial target is **NUCLEO-L476RG**, not the custom PCB. Consult [custom-board porting gate](../Hardware/CUSTOM_PCB_PORTING_CHECKLIST.md) before flashing any custom board.

## GitHub conventions

- `main`: G10 visual lab baseline; G11 custom-PCB architecture and learning additions staged for review; **hardware/ARM validation still pending**. Original G1 is archived under `archive/g1-baseline-before-g4-20261017` for rollback.
- `Core/`: the application that the IDE compiles; `Recovery/`: copies restored after CubeMX; `References/`: original contributors' work, not linked into the current application.
- `Validation/results/`: generated and ignored, not immutable proof. CI artifacts are attached to the specific commit.
- Firmware changes must include portable regressions; board-affecting changes must identify pin/peripheral impact and bring-up checks.
- Do not present unimplemented TinyML, PDM microphone, USB-C battery charging, custom-PCB control, or calibrated audio safety as finished features.

## Current target distinction

**Final demonstrator:** self-powered custom PCB, two 26650 LiFePO4 series cells (3.25 V nominal each), no Nucleo/PC. **Existing integrated source:** STM32 Nucleo firmware baseline until PCB-specific peripheral mapping, control hardware, microphone DFSDM driver and measured output safety are verified. Use [G11 integration/black-box guide](G11_STANDALONE_CUSTOM_PCB_RELEASE_PLAN.md) and the [interactive full-system atlas](https://rv-crtl.github.io/Digital-Hearing-Aid-G1/#architecture).
