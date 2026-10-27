# G1 standalone custom PCB — whole-system requirements and black-box demonstration plan

**Document type:** draft technician-executable test plan, **NOT** evidence that tests have passed. Set each TBD numeric acceptance threshold from real calibrated prototype measurements and freeze the values before the assessment. This is a standalone hardware test with **two 26650 LiFePO₄ cells in series** (3.25 V nominal per cell), no Nucleo/PC/ST-Link/serial debug connection. Target duration **19 minutes** (hard rubric maximum 30 minutes).

**Device identification:** PCB revision [TBD], serial [TBD], STM32 marking [TBD], source Git SHA [TBD], actual ELF/HEX SHA-256 [TBD], LiFePO₄ cell manufacturer/pack protection [TBD], enclosure revision [TBD], signed operator [TBD], date [TBD].

## A. Whole-system requirements and coverage

| Requirement | Observable whole-system requirement | Verification | Quantitative limit to freeze |
| --- | --- | --- | --- |
| SYS-01 | Complete unit operates from its intended onboard 2S battery supply without any developer/debug connection | TC01 | Supported pack operating envelope [TBD]; successful self-start within [TBD] s |
| SYS-02 | Unit receives and plays Bluetooth audio with unambiguous user control and recovery | TC02 | Reconnect/failover within [TBD] s; no uncommanded high output |
| SYS-03 | Telecoil mode receives a repeatable loop signal and rejects ordinary nearby interference | TC03 | Detectable output from stated loop level; max unwanted noise [TBD] |
| SYS-04 | Onboard digital microphone provides usable ambient acoustic input | TC04 | Reference source distance/loudness; response and THD/noise [TBD] |
| SYS-05 | Physical PCB controls select modes and user attenuation unambiguously | TC05 | All intended gestures recognized, response ≤[TBD] ms, no unexpected source selection |
| SYS-06 | Audio remains responsive while multiple inputs/user events occur | TC02, TC03, TC05, TC07 | Max dropout [TBD], audible clicks [TBD], successful recovery without reset |
| SYS-07 | Adaptive preferences learn from explicit user corrections and generalize only within a context | TC06 | At least [TBD] repeat observations; bounded adaptation; no boost above calibrated ceiling |
| SYS-08 | Battery, regulator and power path remain within intended operation and recover from common faults | TC01, TC07 | Peak/idle current limits [TBD], minimum loaded pack rail [TBD] |
| SYS-09 | Mechanical enclosure safely retains PCB, two 26650 cells, controls, transducer and microphone port | TC08 | No exposed contacts, loose parts, binding caps or interference |
| SYS-10 | Unit respects specified output limits and does not produce unsafe/unstable sound | TC02, TC04, TC07 | Calibrated maximum acoustic output at coupling point [TBD] |

**Unimplemented requirements:** Do not omit TC04/TC06 just to save time if microphone and AI are part of claimed functionality. If either path is not genuinely on the flashed custom PCB, record FAIL / NOT_RUN as appropriate and repair the engineering before the demonstration. A standalone simulator or a Nucleo-host regression is not a substitute.

## B. External test procedures — 19-minute outline

### TC01 · Self-power and initial state · 2:00 (SYS-01, SYS-08)
**Prerequisites:** Fully assembled enclosure with protected correctly connected 2S LiFePO₄ cells at a measured in-range voltage, connected safe speaker/load; pre-programmed device; no PC/USB/Nucleo/ST-Link. Calibrated meter available.

**Procedure:** (1) Inspect battery polarity and security before connecting. (2) Turn on through normal switch. (3) Confirm user-visible start indication and stable audio idle. (4) Turn off/on once. (5) Measure external pack voltage without opening live inaccessible contacts.

**Expected result:** Predictable initial mode and no reset, pop or uncontrolled output. Startup time ≤[TBD] s; pack voltage within [TBD] V. Record exact values and PASS/FAIL.

### TC02 · Bluetooth audio and lost-link recovery · 3:00 (SYS-02, SYS-06, SYS-10)
**Prerequisites:** Known reference music/speech test file on a phone or external Bluetooth audio source, fixed output distance and safe level.

**Procedure:** (1) Select Bluetooth using onboard UI. (2) Play reference track. (3) Change its playback state and confirm expected response. (4) Disable source connection then restore it, without reset. (5) Repeat once in a different command order.

**Expected result:** Consistent audio and reconnection/recovery within [TBD] s, no audible burst above [TBD] calibrated limit, no reboot. Record each source change.

### TC03 · Telecoil reception and interference tolerance · 3:00 (SYS-03, SYS-06)
**Prerequisites:** Known induction-loop source and safe acoustic listening/coupler.

**Procedure:** (1) Select telecoil from physical controls. (2) Apply calibrated loop signal. (3) Move/rotate the coil relative to field and document the audio change. (4) Remove the field. (5) Reapply signal and change modes back and forth.

**Expected result:** Output follows field and source selection; false-trigger interference below [TBD] limit. No need for internal SAI probing or debug UART.

### TC04 · Microphone distance response · 2:00 (SYS-04, SYS-10)
**Prerequisites:** Calibrated external sound source and sound-level measurement arrangement, selected microphone mode if implemented.

**Procedure:** (1) Provide a known speech or tone source at two declared distances. (2) Observe transducer output and system input selection. (3) Restore distance and confirm repeatable response.

**Expected result:** Document actual microphone audio path and response [TBD], without clipping or unstable acoustic feedback. If DFSDM mic route is not integrated on final build, record NOT_RUN / FAIL; do not substitute Bluetooth.

### TC05 · Controls, rapid changes and event ordering · 3:00 (SYS-05, SYS-06)
**Prerequisites:** Standalone onboard encoder/touch/buttons labelled with intended behaviour.

**Procedure:** (1) Set mode/profile and change volume with physical UI. (2) Send rapid and alternating inputs. (3) Select BT→TC→mic (if present), then power cycle. (4) Recover from accidental double input.

**Expected result:** Correct deterministic physical state, no unexpected audio spike, button motion/encoder detents reliable, UI latency ≤[TBD] ms. Record any dropped gesture.

### TC06 · Contextual AI preference loop · 3:00 (SYS-07)
**Prerequisites:** **Actual onboard** feedback path connected to tested learner, user opt-in and calibrated attenuation ceiling. Two repeatable acoustic contexts, source and level fixed.

**Procedure:** (1) Choose the initial context. (2) Record initial output. (3) Apply repeated user down-volume corrections. (4) Repeat same stimulus and observe the bounded response. (5) Change frequency/amplitude/context: verify unrelated cells remain unchanged. (6) Increase user preference: verify it cannot exceed allowed output gain.

**Expected result:** Learned context-specific attenuation, repeatable updates after required feedback, override accepted, no gain beyond unity in digital learner, no measurable unsafe SPL. Otherwise NOT_RUN / FAIL. Browser learning demo alone does not satisfy this test.

### TC07 · Normal faults, recovery and safe supply · 2:00 (SYS-06, SYS-08, SYS-10)
**Prerequisites:** Test procedures approved for safe fault induction only; appropriate current-limited supply if a pack load-change test is needed.

**Procedure:** (1) With a safe external Bluetooth/telecoil stimulus, remove and restore signal. (2) Check the output attenuates or recovers according to requirement. (3) If approved, measure current and regulator at safe conditions using a bench supply at the *end* of functional checks.

**Expected result:** No uncontrolled high output or uncommanded reboot; recovery ≤[TBD] s, power draw ≤[TBD]. **Never short, overcharge, intentionally reverse or heat LiFePO₄ cells.** Abnormal-power tests require supervised electrical test equipment and occur last.

### TC08 · Mechanical finish and user factors · 1:00 (SYS-09)
**Prerequisites:** Fully assembled final housing.

**Procedure:** (1) Inspect case finish and PCB retention. (2) Operate buttons, dial and case lid. (3) Inspect battery isolation, cell restraints, transducer grille and acoustic microphone opening. (4) Check access to intended connectors, labels and finger clearances.

**Expected result:** Firm enclosure; all controls smooth, safe contacts, accessible connectors, removable lid and no obstructions. Record photo evidence with contributors’ consent.

**Total:** 2+3+3+2+3+3+2+1 = **19 minutes**.

## C. Required evidence and assessment workflow

Print two **A3** copies of [the system and task diagram](../docs/print-architecture.html), and bring the final group source link, populated BOM/schematic and test-plan PDF. Record actual external observation, instrument, units, acceptance criteria and operator for every test. Ensure no synthetic RTOS stress tasks or unit tests are present in the running microcontroller operating build. The source package and CI can support **viva explanations**, not replace the independent device test.

**Do not mark any test PASS before physically executing it.** Save device/pack/firmware revisions, actual log/proof file hashes and acceptance decisions in the evidence register.
