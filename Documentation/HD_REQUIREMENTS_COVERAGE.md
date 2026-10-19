# Whole-system requirements and black-box coverage register

**Use this as a revision WORKSHEET, not as a claim that all requirements are met.** Only the group/assessors can approve requirement scope changes. Original IDs are preserved from `REQUIREMENTS_EVIDENCE.csv`. Each status must be updated with actual assembled-board measurements before submission.

| ID | Whole-product requirement from current matrix | Black-box test ID | Present evidence gap / gate |
|---|---|---|---|
| FUN-01 | Amplify hearing-range sound | BB01, BB08 | Acoustic transfer/function and calibrated gain not yet measured |
| FUN-02 | Automatic gain adaptation | BB05 | Demonstrate real low/high amplitude controlled sound, output response |
| FUN-03 | Noise reduction | BB05 | Measure before/after signal-to-noise improvement under real stimuli |
| FUN-04 | Feedback suppression | BB09 | **Unimplemented**; must integrate or formally revise scope |
| FUN-05 | Bluetooth audio | BB02, BB06 | Rehearse actual connected module, phone input, interrupted connection |
| FUN-06 | Telecoil audio | BB03, BB06 | Calibrated induction source and physical presence detection |
| FUN-07 | Microphone capture | BB01, BB06 | **Not integrated** into G5 firmware; physical build required |
| FUN-08 | Bone-conduction output | BB01, BB02 | Mechanical coupling and acoustic output measurement |
| PER-01 | Audio latency < 50 ms | BB08 | Measure input/output transient using applicable instrument |
| PER-02 | THD < 3% | BB08 | Use calibrated signal/acquisition instrument and level range |
| PER-03 | Two independent microphone channels | BB01 | **Not integrated**; prove two distinct source responses |
| INT-01 | Physical user volume and mode controls | BB04, BB06 | G5 Nucleo B1 exists; custom PCB encoder/touch unverified |
| INT-02 | Wear detection | BB07 | **Not integrated**; wear sensor wiring/state not established |
| INT-03 | Source-selection precedence | BB03, BB06 | Observe physical source switch and output, not UART FSM status |
| POW-01 | 24 h operating endurance | BB10 | Cannot reasonably perform full 24 h in demo; provide pre-recorded traced endurance test plus live current cross-check |
| POW-02 | Low power when not worn | BB07, BB10 | Not currently implemented as measured STM32 low-power mode |
| SAF-02 | Safe acoustic output | BB05, BB09 | Requires calibrated coupled output; PCM headroom is NOT an SPL measurement |
| G2-GUARD | Optional DSP overload recovery | BB06 | Host coverage only; externally detectable overload scenario must be safe/repeatable |
| G1-PROFILES | Selectable profiles | BB04 | Real audible difference under controlled signal and physical control |

**Coverage is NOT conformance.** The actual system demonstration must convincingly satisfy *all agreed current system requirements*. A proposed test can still be blocked. Record exact conditions, pass criteria and measured numbers rather than marking `PASS` automatically.

## Requirement revision discipline

For any revised whole-system requirement record: ID, description, *observable metric/threshold*, reason, agreed sign-off, linked BB test, physical hardware/firmware revision and dated result. Avoid subsystem implementation requirements (e.g., “use mutex”, “use ADC1”); those belong to design/viva notes.

## Missing functionality decision

Unless feedback suppression, microphone, two independent mic channels, proximity, battery endurance and output safety are actually available, do not submit a table claiming every requirement was passed. Agree a legitimate scope revision with the group and course staff where necessary. Preserve historical requirements and rationale in version control.
