# BLACK BOX — System Demonstration Test Plan (target 20 minutes)

**G11 standalone custom-PCB draft for assessor approval / technician execution.** This plan follows the *System Demonstration and Viva* template, p. 7. **Not a record of successful tests.** The operating MCU must already be flashed, powered from the intended **two series-connected 26650 LiFePO4 cells (6.50 V nominal pack)**, enclosed, and running **without NUCLEO, PC, ST-LINK or serial debug UART**. Conduct the tests in this sequence. External measurement instruments are permitted where directly tied to a system requirement. A Bluetooth phone is permitted if naturally part of the device.

> **Release gate:** The team reports that the custom-PCB hardware operates, but current-commit integrated evidence is not captured here and **PDM microphone/DFSDM firmware integration remains a source-level gap**. This document cannot be submitted as an “all PASS” plan until those are resolved or scope formally revised. Adjust estimated times through rehearsal; target <=20 min, absolute maximum 30 min.

See also the [G11 integrated bus/ownership and battery acceptance plan](G11_STANDALONE_CUSTOM_PCB_RELEASE_PLAN.md). No microphone or AI performance test may be marked PASS without the actual end-to-end installed function.

## Test coverage table

| Requirement number | Requirement description (current draft) | Test case(s) |
|---|---|---|
| FUN-01 / FUN-07 / FUN-08 / PER-03 | Microphone acquisition and bone-conduction audio | BB01, BB08 |
| FUN-05 | Bluetooth audio | BB02, BB06 |
| FUN-06 / INT-03 | Telecoil and source priority | BB03, BB06 |
| INT-01 / G1-PROFILES | Physical mode/volume/profile control | BB04, BB06 |
| FUN-02 / FUN-03 / SAF-02 | Adaptation, noise handling, output protection | BB05, BB09 |
| INT-02 / POW-02 | Wear detection and standby | BB07, BB10 |
| PER-01 / PER-02 | Latency and distortion | BB08 |
| FUN-04 | Feedback suppression | BB09 |
| POW-01 | Battery endurance | BB10 |
| G2-GUARD | Robust recovery from overload | BB06 |

## Common prerequisites and record fields

**Specimen:** board revision ____; firmware Git SHA ____; enclosure revision ____; supply type/voltage ____; test operator ____; date ____; sound level meter / analyzer model+calibration ____; measured ambient dBA ____; source equipment IDs ____. Confirm safe fixture and volume minimum. No debug lead or UART attached.

For each test record: Actual observation, measured values (with units), PASS/FAIL/BLOCKED, instrument ID, exception/incident, operator initials. Never replace a missing measurement with a precomputed Matlab/C plot.

### BB01 — Live microphone capture (2:00)
**Requirements:** FUN-01, FUN-07, FUN-08, PER-03.

**Prerequisites:** Integrated independent microphones, volume at minimum, bone transducer safely mounted; controlled speech/tone source.

**Procedure:** (1) Select microphone mode using product controls. (2) Present a spoken phrase at the defined near position. (3) Move the source left/right to exercise each microphone channel without a reset. (4) Verify audio remains intelligible at output. (5) Record measured output/relative channels, source distance and gain.

**Expected:** Continuous real acoustic audio, no unexplained reset or dropouts; distinct channel response where dual mic is claimed. **Current status: BLOCKED until full microphone integration.**

### BB02 — Bluetooth playback and reconnection (2:00)
**Requirements:** FUN-05, FUN-08.

**Prerequisites:** Phone paired with actual Bluetooth module; downloaded offline speech/music clip.

**Procedure:** (1) Start playback at safe level. (2) Change phone volume. (3) Pause, resume, disconnect and reconnect. (4) Note audible recovery duration with stopwatch.

**Expected:** Real output follows play/pause/volume, reconnection without MCU reflash/reset, no harmful transient. Define allowable recovery seconds from measured design.

### BB03 — Induction/telecoil and precedence (2:00)
**Requirements:** FUN-06, INT-03.

**Prerequisites:** Known induction source at reproducible distance/orientation; available Bluetooth content.

**Procedure:** (1) Play inductive test source in telecoil mode. (2) Activate Bluetooth simultaneously. (3) Request AUTO via product control. (4) Vary source availability, document which signal is heard.

**Expected:** Actual sources, intentional documented precedence, smooth transition, no deadlock or output burst. Do not infer RF link state purely from PCM validity.

### BB04 — Real user controls and accessible UX (2:00)
**Requirements:** INT-01, G1-PROFILES.

**Prerequisites:** Installed physical controls, labelled functions, safe output level.

**Procedure:** (1) Adjust volume to two clearly different positions. (2) Select profiles in a different order than rehearsal. (3) Toggle mode and restart device once. (4) Confirm whether state retention is an actual requirement.

**Expected:** Prompt observable change, intuitive feedback, no contact bounce or accidental source jump. **Custom PCB B1/PC13 absent: demonstrate only controls truly wired on actual specimen.**

### BB05 — Adaptive response and rejection (2:00)
**Requirements:** FUN-02, FUN-03, SAF-02.

**Prerequisites:** Reproducible speech/noise and level steps; calibrated sound meter or coupler if acoustic quantitative claim made.

**Procedure:** (1) Apply low-level stimulus then higher level at fixed source position. (2) Compare observable outputs and adaptation. (3) Mix speech with broadband interference and enable claimed noise reduction. (4) Measure output with proper fixture.

**Expected:** Bounded output with documented input/output gain change and measured improvement where claimed. **If calibrated SPL or real microphone not available, BLOCKED.**

### BB06 — Unexpected operations and automatic recovery (3:00)
**Requirements:** INT-03, G2-GUARD, FUN-05, FUN-06, INT-01.

**Prerequisites:** Both verified sources, device in ordinary running mode.

**Procedure:** (1) While playing, use an unexpected order of physical controls. (2) Disconnect a *non-power* signal cable / disable radio source. (3) Reintroduce source. (4) Repeat source change while content plays. (5) Log dropout/recovery duration and state after recovery.

**Expected:** Device remains responsive; safe mute or fallback; automatic recovery, no reflash/reset, no severe click/pop. Do not claim guardian specifically triggered without independent evidence. **No intentional power shorts or uncontrolled overload.**

### BB07 — Wear removal and true standby (1:30)
**Requirements:** INT-02, POW-02.

**Prerequisites:** Validated wear detector and genuine low-power transition.

**Procedure:** (1) Remove device / actuate proximity stimulus. (2) Confirm output mute. (3) Use power meter to compare normal vs standby current if quantitative. (4) Restore wearing state.

**Expected:** Reliable mute, measurable current decrease and automatic wake. **Current status: BLOCKED.**

### BB08 — Acoustic performance measurements (2:30)
**Requirements:** FUN-01, PER-01, PER-02, FUN-08.

**Prerequisites:** Calibrated instrument with suitable transducer fixture and known stimulus. Keep sound levels safe.

**Procedure:** (1) Measure propagation delay using identified input/output stimulus and device transfer (Bluetooth transport delay must be included/declared as applicable). (2) Measure fundamental/harmonics at reproducible test levels. (3) Check response at representative hearing-range frequencies.

**Expected:** Total measured latency <50 ms, THD <3%, requirement-defined gain/response. Save raw outputs. If instrument cannot capture physical endpoint appropriately, mark BLOCKED.

### BB09 — Feedback and output safety (1:00, **last risky test**) 
**Requirements:** FUN-04, SAF-02.

**Prerequisites:** Calibrated safe test fixture, limits and termination procedure.

**Procedure:** (1) Present a mild, controlled feedback-prone condition below safe level. (2) Observe suppression and output limit. (3) Stop immediately at excessive output.

**Expected:** No runaway tone, measurable suppression if required, calibrated output below agreed limit. **Current status: BLOCKED: feedback controller unimplemented, calibration absent. Never perform damaging test without protection.**

### BB10 — Power and endurance cross-check (1:00)
**Requirements:** POW-01, POW-02.

**Prerequisites:** Validated intended power source; independently recorded long-duration endurance test, current meter.

**Procedure:** (1) Show traceable prior 24-hour test with battery IDs, conditions and dated data. (2) Measure live normal/standby current without changing product wiring unsafely. (3) Compare current estimate against actual endurance evidence.

**Expected:** Evidence of required endurance; measured current appropriate for battery energy budget. **Current status: BLOCKED without physical endurance records.**

**Budget:** 19 minutes plus a 1-minute transition buffer = **20 minutes**. Place any other potentially damaging tests last, consistent with rubric p. 2. Do not perform destructive tests merely to earn an advanced mark.
