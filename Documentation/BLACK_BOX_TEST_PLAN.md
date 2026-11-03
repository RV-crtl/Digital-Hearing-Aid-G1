# G1 — black-box demonstration test plan

**Version:** 03 November 2026 · **Assessment:** System Demonstration and Viva (ZEIT4232) · **Target duration:** 18–20 minutes (30 min maximum). This is a **planned procedure, not a record of passes**. Report actual data in [the result sheet](BLACK_BOX_RESULTS_TEMPLATE.md). Assessor should be able to execute it without reading code or connecting a debugger.

## Preconditions and safety

- Custom PCB is flashed with the **whole-project R24/R26 operating candidate** that has been successfully built and separately verified. ST-LINK, Nucleo and diagnostic UART remain **fully disconnected** for this black-box demonstration.
- Use the intended protected and regulated standalone supply. Prior user tests established untethered **USB-C operation of R24**; battery-only operation must be verified separately before claiming it.
- Keep sound output on a **safe bench speaker/load**, not in an ear, until maximum electroacoustic output and feedback behaviour are calibrated. Start muted or with a safe physical output attenuator; 100/100 UI level does not establish acoustic safety.
- Ready: Bluetooth source phone with a known audio clip; induction-loop test fixture with a safe 2 kHz test audio source; sound source for microphone speech/ambient scene; timer and sound-level meter where available; printed requirements sheet.
- No PC-driven commands, source inspection, reflash, forced debugger resets or internal probes in core tests. Phone is allowed as a natural Bluetooth audio source.
- Proposed response targets below are **test criteria to agree and check**, not measurements already made.

## Requirement-to-test coverage

| ID | Whole-system requirement | Test | Provisional acceptance criterion |
| --- | --- | --- | --- |
| G1-01 | Boot and operate independently | T1, T7 | No Nucleo/ST-LINK/console; 3 consecutive cold starts |
| G1-02 | Capture room audio and output audible signal | T2 | Both near and distant speech present on test fixture; record distance, level and noise |
| G1-03 | Accept and reproduce Bluetooth audio | T3 | Pair/play/pause/restart, recover after phone disconnect |
| G1-04 | Receive induction-loop/TC audio | T4 | 2 kHz signal audible with TC source; suppress idle hiss when stimulus stops |
| G1-05 | Reliable local touch and rotary controls | T2, T5 | ≥5/5 intentional presses per electrode; volume monotonic per detent |
| G1-06 | Adapt audio level without uncontrolled distortion | T2, T4 | Bounded gain; compare quieter/louder stimuli without clipping by observation/instrument |
| G1-07 | Respond robustly to unexpected source/control input | T5, T7 | Changes behave consistently without restart or reflash |
| G1-08 | Demonstrate advanced adaptation / interference rejection | T6 | Repeatable input-dependent gain/noise behaviour; document limits of 3-cluster ML |
| G1-09 | Safe, reliable continuous operation | T7 | Continuous audio; no spontaneous mute/dropout/reboot during test |

## Demonstration sequence

| Test | Budget | Technician procedure | Record / expected observable outcome |
| --- | --- | --- | --- |
| **T1 — Untethered boot** | 2 min | Power-cycle with ST-LINK and Nucleo absent. Select MIC via E11. | Audio starts without console. Note boot time, source and whether any reset was needed. |
| **T2 — Ambient sound & local control** | 4 min | Speak at 0.5 m and 2–3 m (record actual distances); play a representative ambience clip with voices/traffic; rotate ±3 encoder detents; touch E10 mute twice. | Record clarity, environmental pickup, whether steps are monotonic (no reverse bounce) and mute/unmute responsiveness. Avoid unsafe loudness. |
| **T3 — Bluetooth** | 3 min | Touch E8, pair phone, play music/speech at low external level, pause/play, break and restore BT link. | Note pairing time, recovered audio, source transitions and no crash. Do not claim phone volume control without evidence. |
| **T4 — Telecoil and interference** | 3 min | Touch E9; turn induction-loop test on at 2 kHz, then off, then on again. Compare stimulus present/absent and at two safe levels. | TC audio should be audible via LOW16 path, FILTER ON; record residual hiss, fade/recovery and audibility without serial logs. |
| **T5 — Rapid user inputs** | 2 min | Execute E11→E8→E9→E11; use E10 mute; rotate ±5 detents; test each touch electrode intentionally five times. | Log missed/double activations, wrong-direction volume steps, transitions and recovery. |
| **T6 — Adaptive response** | 3 min | Hold source/distance fixed and apply quiet then louder microphone stimulus; repeat short ambient / tonal signal sequence; listen and/or meter the dynamic level and suppression. | Demonstrate AGC/TC closed-loop effect. **Scene clusters are unlabelled**; do not claim supervised vehicle/person recognition. If no reproducible difference, record a failure rather than simulation. |
| **T7 — Robustness / restart** | 2–3 min | Return to MIC, disconnect/reconnect BT source, perform an orderly power cycle with no ST-LINK. Observe for glitch/reboot while using touch and encoder. | No spontaneous crash; normal startup. Record any dropout and number of cold-start successes. |

**Expected total:** approximately 19–20 minutes. If an experiment fails, record the symptom and continue safely. Do not rewrite a result as PASS.

## Evidence / observability

For each test capture **PASS/FAIL/NOT RUN**, observed behaviour, elapsed time (where applicable), stimulus details, test hardware and operator initials. Do not mix source-code/host regression evidence with black-box product results. Quantities such as SPL, SNR, latency, power draw and adaptive gains have **no verified thresholds** until the test team calibrates the fixtures.

## Optional engineering evidence (after the black-box run)

Use the debug build, oscilloscope or UART only *after* completing black-box tests to investigate timing, DMA/RTOS priorities, task headroom, source errors or PCM alignment. Those are technical diagnostics, **not** substitutes for the independent system demonstration.

See [current requirements](G1_CURRENT_SYSTEM_REQUIREMENTS.md) and [diagram](PRINTABLE_SYSTEM_DIAGRAM.md).
