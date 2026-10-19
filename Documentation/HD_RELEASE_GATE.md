# Release gate — ZEIT4232 operating system demonstration

**Hard stop**: do not label a build `DEMO-READY` unless all agreed whole-system requirements have a dated physical PASS record, enclosure/accessibility is checked, and the full black-box plan is rehearsed <=30 min (aim 10–20).

- [ ] Written and group-approved *revised whole-system* requirements; no silent deletion of failed requirements.
- [ ] Every requirement linked to an executable, externally observable test (no code inspection, UART or debugger as demo evidence).
- [ ] Combined revised-requirements + test-plan PDF submitted to Moodle.
- [ ] Firmware commit SHA/release tag recorded, flashed before appointment.
- [ ] User-facing operating build **without unit tests, synthetic source demonstrations, or dummy CPU spin/stress tasks**; engineering-only commands cannot accidentally disrupt it.
- [ ] Board starts/stays operational with intended battery/AC supply and no ST-LINK/debug serial connection.
- [ ] User controls physically present on chosen board, observable response in bounded time.
- [ ] Source unplug/replug, input order and distance changes, ordinary supply tolerance rehearsed without firmware reset/reflash.
- [ ] Safety/overload tests only under controlled protection, performed last.
- [ ] Sound performance measurements linked to quantitative requirements; instrument model, calibration, conditions and raw observations recorded.
- [ ] Whole firmware compiles for exact target PCB/Nucleo; config and ISR RTOS priorities checked.
- [ ] Mechanical pass criteria: custom enclosure, PCB retention, connector access, microphone ports/transducer protection, easy lid removal.
- [ ] Printed two system diagrams showing buses, hardware parts, tasks/queues, data/control flow and annotated source ownership.
- [ ] Each viva candidate can locate own modules, explain cross-team interfaces, demonstrate peripheral ownership, buffer ownership and error handling.
- [ ] **Known blockers resolved**: microphone/dual mic, physical controls, wear/power, charging if claimed, calibrated sound safety, feedback suppression, all agreed power/performance figures.

## Traceable test record template

`record_id, requirement_id, test_id, hardware_rev, firmware_sha, operator, date_time, instrument_id, condition, expected, actual, units, status, photo_video_or_raw_data_uri, reviewer`

Allowed statuses: PASS, FAIL, BLOCKED, NOT_RUN. Never auto-mark PASS because a CI job succeeded.

## Assessment anti-patterns

- A successful MATLAB impulse plot is *not* proof of 50 ms product audio latency.
- A FreeRTOS task list in a debugger is *not* an external black-box requirement test.
- Demonstrating a partial microphone source with test sine waves is *not* real mic integration.
- A 5 V USB-C connector is *not* a two-cell LiFePO4 charger or 24-hour battery test.
- A feature shown once in fixed order is *not* evidence of robustness.
