# G11: standalone custom-PCB architecture, adaptive learning and HD demonstration gate

> **Target product definition (2026-10-28):** one portable standalone custom PCB, powered by **two 26650 LiFePO₄ cells in series** (3.25 V nominal/cell, **6.5 V nominal pack**). No Nucleo, computer, serial debugger, ST-Link or PC power connection is part of the normal operating product. This replaces the previously Nucleo-centred *demonstration wording*, **not** the firmware porting or hardware testing evidence. The user reports the assembled hardware operates; this repository does not yet contain the measured final-PCB evidence needed to independently establish all claims.

The required course rubric is [System Demonstration and Viva](../Documentation/Reference/Individual_Demonstration_Rubric.pdf), alongside the user's updated 7-page *System Demonstration and Viva* copy. The following engineering criteria retain the rubric's distinctions: demonstration (15%), mechanical prototype (5%), individual viva (20%), total 40%.

## 1. Hard acceptance gates, not website marketing

| Rubric requirement | Observable external demonstration | Repository's current evidence | Remaining acceptance |
|---|---|---|---|
| One integrated standalone product | Insert / connect intended 2S pack, switch on, produce audio without a PC/Nucleo/ST-Link | KiCad + Nucleo-oriented firmware source, standalone demo paperwork | Verify pin/peripheral and clock mapping on **actual** PCB; install signed evidence/video; pack voltage/polarity safe |
| All **whole-system** requirements tested in 10–20 minutes (30 max) | One technician executes numbered external tests; measured values recorded | Existing Assessment test plan and requirements matrix | Replace inaccurate Nucleo assumptions, add calibrated thresholds and field evidence |
| Intuitive controls, unexpected button sequences | Random short/long/touch/encoder sequences; observable audio feedback | Nucleo PC13/PA5 gestures in `demo_app.c`; PCB encoder PA0/PA1 design pins | Implement/verify PCB UI drivers; avoid claiming Nucleo buttons exist on product |
| Microphone | Bring real sound source nearer/farther, observe output and latency | `Hardware/STM32_U13_PINMAP.csv`: DFSDM1_CKOUT pin 10, DFSDM1_DATIN2 pin 35; schematic | DFSDM capture, PDM clock/mic initialization, DMA/routing, acoustics **not established in checked-in firmware** |
| Real-time tasks & synchronization | No glitches while changing source/user controls; tasks remain responsive | Source maps DMA completion -> notify -> AudioTask, 8-entry `GroupEvent` queue | Board profiling: measured deadlines, highest stack use, overrun/underrun counts, traceable captures |
| Gain/feedback adaptation | Observe context and user correction, replay same condition, compare response | New 27-context portable learner, C mutation/portable tests; site simulator | Connect real custom-PCB volume feedback to queue, opt-in/persistence, prevent oscillation, measure gain/acoustic cap |
| Fault recovery & self-protection | Disconnect permitted input, rearrange commands, brownout within specified envelope; recover without reflash | Portable guardian/source FSM, bounded queues; some physical data not archived | Lab test pack limits, output mute under faults, amp/transducer thermal/current protection, overvoltage tests last |
| Mechanical fit & finish | Case closes, PCB retained without flex, connectors accessible, tactile feedback reliable | Supplied real 3MF assembly and KiCad design | Signed observations: battery restraint, isolation, lid removal, labels, speaker grille, microphone acoustic opening, ventilation |
| Viva source/interface explanations | A3 printed block diagram, locate ISR, task, queue, DMA, battery and BOM | Interactive System Atlas, code browser, source docs | Print A3, verify actual PCB bus wiring and names, practise replacement/troubleshooting scenarios |

The rubric **explicitly requires**: real self-powered use without ST-Link/serial debugger; no unit-test tasks or dummy CPU loading in operating image; systems tests based on *external black-box behaviour*, not debugger internals; valid repeatable test procedures; two printed system diagrams; and A3 system diagram / code / schematic-BOM for viva.

## 2. One-page complete system map

The [System Atlas](../docs/index.html#atlas) is an **interactive and source-linked** diagram: 2S LiFePO₄ -> power rails -> STM32; Bluetooth and telecoil -> SAI RX DMA -> half/full callbacks -> capture rings -> notification -> AudioTask -> DSP -> SAI2B TX DMA -> amplifier/transducer. It also shows the **DFSDM microphone design path as an integration gate**, not a tested capture path. ControlTask -> copied, bounded `GroupEvent` queue -> AudioTask; Bluetooth supervisor and background task own neither audio buffer nor DMA. Click nodes for linked implementation.

**Representative sample budget:** at 48 kHz, 128 frames are 2.667 ms of sampled time, not proof of 2.667 ms scheduling latency. A 24-bit signed PCM sample held in one 32-bit SAI word occupies 4 bytes in the DMA array; two channels have two words/frame; a 128-frame stereo half-block requires 1024 bytes before internal 16-bit conversion. *Actual* DMA buffer dimensions and source alignment must be read from the configured `demo_app.c` symbols and confirmed on the PCB. Capture ring depth is eight; output sample FIFO holds 4096 samples in the current implementation. ISR publishes before task-notification.

The source tree still targets the NUCLEO-L476RG build, which is useful as a reproducible **firmware baseline**, but the physical product uses the custom board. The following change is a non-negotiable board port:

1. Choose final power pins, oscillator, NVIC priorities and I/O alternate functions from the **original KiCad** and `Hardware/STM32_U13_PINMAP.csv`, not the Nucleo template.
2. Replace PC13 B1 / PA5 LD2 demonstration assumptions with actual wired rotary / touch / button inputs on the PCB. The original PCB map has PC13 and PA5 unconnected at the MCU.
3. Bring up and route the actual DFSDM mic if it is a required input. Verify input clock and noise floor, then PCM framing and source policy, without affecting time-critical SAI.
4. Add power-on self-check: rail stability, undervoltage/fault policy, controlled amplifier start, no output pop or unrestricted audio level.
5. Run **physical** regression with real user inputs and 2S power pack; capture measurable results and update acceptance matrix. Do not hardwire unverified settings merely to make the website appear complete.

## 3. On-device contextual preference learning — implementation

`Core/Src/preference_learner.c` and `Core/Inc/preference_learner.h` contain a **real, deterministic, bounded online learner**, not a claim of pretrained neural-network classification.

- **Features:** three loudness bins (from actual PCM average magnitude); three coarse spectral bins using adjacent sample differences (high/low-frequency proxy, not FFT); three input contexts (BT, telecoil, microphone). This yields 27 independent, fixed-size state cells.
- **Feedback:** explicit up/down preference corrections, never inferred from a silent environment. `G1Pref_Feedback` learns incremental signed preferences; 250 ms minimum feedback interval, confidence starts after two accepted actions, count saturates at 32.
- **Inference:** `G1Pref_RecommendQ15` recommends a **relative attenuation-only Q15 factor**, in [24576, 32768], so it never boosts the input signal. Explicit increase requests can only reduce existing attenuation. This is **not** a calibrated ear-level acoustic safety limit.
- **Ownership:** single writer inside AudioTask when integrated; ControlTask posts compact feedback events to bounded queue. Learner does no heap allocation, filesystem access, network access or flash writes in real-time processing.
- **Current boundary:** portable module is compiled and tested (including sanitizers), and the website has an interactive matching educational demo. **The custom PCB event path, real spectral inference integration, persistent preferences, calibrated acoustic cap and performance profiling remain to be implemented/verified.**
- **Advanced extension roadmap:** multiband filter-bank or FFT features, environmental state clustering, short-term vs long-term reinforcement with decaying confidence, user opt-in/forget, policy drift detection, noise/feedback detection, frequency-specific response, off-ear calibration and evaluation of latency, power and preference accuracy. No attempt to label untrained cells as a validated hearing-benefit model.

## 4. External black-box demonstration sequence (target 19 minutes)

This is a **rehearsal plan**. Each step only receives PASS after an operator records measured observations and the final build/board. Test case IDs must be cross-referenced to the final course combined PDF.

| Time | External action, no debugger | Pass observation / quantitative threshold |
|---|---|---|
| 0:00–2:00 | Connect approved protected 2S LiFePO₄ pack, switch on, listen for start-up; no PC/Nucleo | Stable self-start, no unexplained restart, safe output at selected level. Record source voltage and idle current with appropriate instruments after functionality demo |
| 2:00–5:00 | Bluetooth input, connection/loss/recovery, manual source selection | Output follows selected source; no unsafe burst. Set required reconnect time and number of consecutive attempts before test |
| 5:00–8:00 | Telecoil loop pickup, moved/rotated/removed | Predictable pickup and fallback, audible noise within stated threshold |
| 8:00–10:00 | Real microphone sound source moved nearer/farther, if implemented | Meaningful externally observable acoustic difference with defined test sound level and range; if unimplemented mark **NOT_RUN**, not PASS |
| 10:00–13:00 | Physical rotary/touch button sequence, fast changes, simultaneous input | Deterministic volume/profile feedback without audio dropouts; define maximum UI response time and no clipping condition |
| 13:00–16:00 | Repeat same loudness/frequency context, press lower/raise, replay | Compare deterministic response before/after same-context training (if integrated); accept only bounded adaptation and user override |
| 16:00–18:00 | Disconnect source, induce ordinary recoverable faults within device limits | Recover without reflash/reset within pre-declared time; sustained audio stability |
| 18:00–19:00 | Mechanical inspect enclosure, case lid, battery retention, microphone opening and speaker mounting | All fasteners intact, no short/exposed conductor, no case interference; document observable deficiencies |

**Do not deliberately short-circuit a battery or speaker, reverse pack polarity, overcharge cells or expose users to uncalibrated loud sound.** Any destructive-condition tests need approved equipment, current limiting and trained supervision and go last as the rubric requires. Set numeric thresholds from your real validated requirement table before the examination, never invent measurement values.

## 5. Mechanical HD rubric checklist

PCB fixed with serviceable standoffs/fasteners and no bending; matched battery retention and insulated 2-cell terminals; buttons and rotary caps with reliable travel; accessible connectors; removable lid with fit and finish; microphone acoustic port; speaker/transducer guarded mounting; wiring strain relief; LED light pipes if actual LEDs exist; printed/embossed control labels; fillets/chamfers, ergonomic reach and a documented more-than-PLA improvement **only if shown to improve performance**. The supplied 3MF is a design asset, not evidence of completed assembly or manufacturing quality.

## 6. Public repository and release workflows

The GitHub repo is presently **public** but lacks a team-authorised project-wide licence. Mark all source/test artifacts as an engineering prototype. Configure About description, official Pages URL/topics, issue templates and Dependabot through reviewed PRs; enable branch protection and private vulnerability reports manually in Settings. Do not create a medical-device binary release or auto-publish firmware until source rights, exact flashed build, board/pack specs and signed measured tests have been accepted.

### Viva rapid navigation

- **Peripheral owner and task priority:** `Documentation/ARCHITECTURE.md`, `Core/Src/demo_app.c`; inspect ISR -> notification -> AudioTask vs GroupEvent control queue.
- **Synchronization and race boundaries:** `Core/Src/demo_app.c`, `Core/Src/group_ui.c`; explain single-writer state, bounded queue and producer-consumer DMA ring.
- **Faults and numerical safety:** `Core/Src/group_guardian.c`, `Core/Src/audio_processing.c`, `Core/Src/audio_dynamics.c`; guardian is a DSP bypass, not a substitute for electrical/acoustic protection.
- **Mechanical/power:** `Hardware/Original_KiCad/Power.kicad_sch`, `Hardware/STM32_U13_PINMAP.csv`, supplied 3MF; check against physical 2S battery assembly.
- **Changed design / troubleshooting:** identify a peripheral's bus, protocol, ownership, data format, scheduling, initialization order and replacement interface; describe how to distinguish DMA underruns from CPU starvation and supply noise.
