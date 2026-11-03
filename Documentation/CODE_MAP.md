# G1 code map · current board architecture versus repository root

**Updated 03 November 2026.** The root `Core/` is the historical Nucleo-oriented CubeIDE project with portable R26 DSP/control modules. It is **not the whole matched custom-PCB R26 image**. The latest custom-board application was delivered separately and requires a clean native GCC build before promoting the root to it. See [current requirements](G1_CURRENT_SYSTEM_REQUIREMENTS.md).

## Live custom-PCB firmware modules (from R26 series)

| Module | Purpose / ownership | Verification status |
| --- | --- | --- |
| `g1_product_board.c/.h` | Real custom-PCB DFSDM/PDM microphone, MPR121 on PB6/PB7 and rotary PA0/PA1; producer to AudioTask | In separate R26 ZIP; **not** root integrated |
| `g1_user_controls.c/.h` | Hardware-neutral MIC/BT/TC/mute/volume policy; ControlTask sole writer | Ported into root; portable control tests |
| `g1_ambient_dsp.c/.h` | DC rejection, 20× bounded AGC and 3-cluster feature extraction; AudioTask sole owner | Ported into root; R26 host DSP tests |
| `g1_scene_ml.c/.h` | Three incremental unsupervised acoustic centroids; bounded O(3) per block, unlabelled | Ported into root; host tests |
| `tc_cleanup.c/.h` | Telecoil band/noise cleanup, idle suppression and adaptive level recovery, preserving LOW16 data path | Ported into root; host tests |
| `adaptive_preferences.c/.h` | Bounded preference offsets from deliberate encoder input; no autonomous high-gain excursions | Portable unit tests; full board integration separate |
| `demo_app.c` (R26 release package) | Real source switching, DMA callbacks, audio FIFO, RTOS tasks, ML/DSP integration | **Not** root; independent whole-package GCC and target measurements pending |

## Repository root (legacy but useful)

| Path | Responsibility |
| --- | --- |
| `Core/Src/demo_app.c` | Historical source state machine, FreeRTOS AudioTask/ControlTask, DMA snapshots and group DSP; not identical to R26 custom board |
| `Core/Src/pcm_transport.c` | PCM transport/ring helpers |
| `Core/Src/speaker_output.c` | SAI audio speaker output |
| `Core/Src/group_ui.c` / `group_encoder.c` | Earlier standalone Nucleo controls, kept for reference/tests |
| `Core/Src/group_guardian.c` | Overload/fault-pressure policy |
| `Core/Src/connectivity_*.c` | Source arbitration and Bluetooth/telecoil state transitions |
| `Recovery/` | Gold copy restored after CubeMX code generation; change relevant mirrors together |
| `PREPARE_PROJECT.ps1` | Dependency preparation and recovery-script guards |
| `Tests/`, `Validation/` | Pure-C tests, independent vector verification, sanitizers and host throughput |

## Contracts and invariants

- **DMA/ISR:** move or snapshot bounded data and notify AudioTask. No UART printing, floating-point DSP or unbounded waits in ISR.
- **AudioTask:** sole DSP state writer, consumes audio blocks, processes 128-sample frames at nominal 48 kSa/s (2.667 ms), then publishes FIFO output to SAI2B.
- **ControlTask:** debounces MPR121/encoder, updates the bounded user policy and sends control events to AudioTask, not samples via a queue.
- **BtTask:** module status control via USART1. USART2 is an optional *off-by-default* diagnostic UART in the custom-PCB operating build.
- **Recovery:** if a file is mirrored under `Recovery/Core/`, edit both or it will silently revert after CubeMX Generate Code.
- **Hardware:** actual custom board uses PC2/PB14 PDM, PB6/PB7 MPR121, PA0/PA1 encoder. Do not derive these pins from Nucleo B1/LD2 demonstrations.
- **ML:** adaptive clusters are real unsupervised feature prototypes; labelled traffic, human or vehicle classification has not been trained or evaluated.
- **Safety:** digital clipping limits are not calibrated acoustic SPL protection.

## Optimisation procedure

To optimise the large application safely: (1) capture the R24/R26 working board and binary evidence, (2) extract one single-owner component at a time, (3) run native CubeIDE GCC build and existing portable regression tests, (4) measure STM32 deadlines/stack headroom and listen on the safe fixture, (5) only then remove old implementations. See [quantitative profiling guide](R26_OPTIMISATION_NOTES.md). Avoid merging code modules from different R-series generations without their matching clock, DMA and board dependencies.

## Find the current demo

[Architecture](PRINTABLE_SYSTEM_DIAGRAM.md) · [Requirements](G1_CURRENT_SYSTEM_REQUIREMENTS.md) · [Test plan](BLACK_BOX_TEST_PLAN.md) · [Real CAD](CAD_ASSEMBLY_AND_VIEWERS.md) · [KiCad original](../Hardware/Original_KiCad).
