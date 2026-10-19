# ZEIT4232 — HD demonstration readiness (authoritative rubric crosswalk)

**Source:** *System Demonstration and Viva*, supplied 20 October 2026, pp. 1–7. These are assessment requirements, not optional polish.

## The grading dependency

The **Minimum** criteria and **Meets Expectations** criteria must hold *before* Exceeds Expectations earns a Distinction/HD. A novel classification algorithm does not compensate for missing microphone functionality, unsafe wiring, unclear tests or an unreliable prototype.

| Rubric area | Absolute gate | Exceeds-expectations evidence |
| --- | --- | --- |
| System demo (15%) | All **current whole-system** requirements demonstrably work; technician can repeat 10–20 min black-box plan; no reset/reflash; unexpected action order tolerated | Application-optimised operation; recovery from disconnections, unexpected input/voltage; self-protection; adaptive operation, classification, interference rejection |
| Mechanical final prototype (5%) | PCB **secured in custom case**, connectors accessible, buttons operable, microphone opening and screen cutout if fitted | Removable well-fitted lid, properly fixed transducer/grill, sensible cable paths, labelled controls; superior finish, chamfers/fillets, ergonomic design, appropriate advanced mechanical functions |
| Individual viva (20%) | A3+ system diagram; latest shared GitHub code; schematic + BOM; identify your own code, functions and interactions with team members | Explain why interface contracts, resource ownership, deadlines, queue selection, DSP numerical behavior and error recovery are designed as they are |
| Before demo | Printed **two** hardware/software diagrams; printed revised whole-system requirements; final group code available; preflashed MCU | Rigorously versioned evidence and contingency actions |
| Operating mode | **No unit tests or dummy tasks** running on STM32; no ST-LINK/debug UART; intended power supply | Reliable start/stop, degradation/recovery; no disruption |
| Submission | **One combined PDF** of revised requirements + black-box test plan via Moodle | Technical clarity, no missing assumptions |

Read [black-box plan](BLACK_BOX_TEST_PLAN.md), [scope and coverage](HD_REQUIREMENTS_COVERAGE.md), [viva interface contracts](VIVA_INTERFACE_MAP.md), [mechanical acceptance](HD_MECHANICAL_ACCEPTANCE.md) and [operating release gate](HD_RELEASE_GATE.md).

## Critical truth about the current repository

- Nucleo G5 and custom KiCad group board are **different hardware targets**; G5 button on PC13 is not a working physical custom-PCB control.
- FreeRTOS debug CPU profiler and host DSP MATLAB plots are appropriate **development/viva evidence**, **not** primary black-box demonstration proof.
- The 20-second priority-7 spin, background CPU-stress experiment and synthetic test-tone commands are **laboratory experiments**, not a production user feature. A demonstrator operating build must disable test-only paths instead of merely promising not to press their controls.
- Neither automatic volume heuristics nor numerical biquad simulations prove trained ML, calibrated acoustic safety, or power endurance.
- A phone **is permitted** if the product naturally uses Bluetooth/USB; the rubric does not prohibit phones or PCs categorically. However, it *does* expect no ST-LINK or serial debugger connected to the operating product (p. 1).
- The group must agree and submit an accurate **revised system-level** requirement set. Missing functionality must be implemented or its replacement/scope adjustment must be explicitly authorised by the group/assessors, never hidden.

## Suggested next engineering order

1. Physically audit assembled board and controls; choose one exact deployable hardware target, power architecture and documented firmware SHA.
2. Integrate microphone and verify each end-to-end source; profile true sustained audio timing; remove production dummy/stress tasks.
3. Add externally verifiable fallback, controlled-source interference response, gain transitions and fault recovery, then measure them.
4. Validate power source, acoustic levels, casing, strain relief and accessibility.
5. Reconcile each requirement with a black-box test and attach signed result entries. Only then advertise HD-level advanced behavior.
