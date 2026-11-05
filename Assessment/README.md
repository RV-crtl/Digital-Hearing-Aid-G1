# ZEIT4232 system demonstration + viva — HD evidence pack

> **STATUS: DRAFT, NOT A PASS CERTIFICATE.** The rubric rewards *observed system behaviour*. Host CI, simulations and inspected source do not replace end-to-end system testing. Group approval is needed before submitting revised requirements.

## Start here (the five things to take to exam week)

| Item | Source / output | Required action |
|---|---|---|
| **1. Revised requirements + black-box test plan** | [`requirements.csv`](requirements.csv) + [`test_cases.json`](test_cases.json) → `generated/SYSTEM_REQUIREMENTS_AND_BLACK_BOX_TEST_PLAN_DRAFT.pdf` | Review, make requirements realistic, obtain group approval, and upload **one combined PDF** to Moodle |
| **2. System architecture poster** | `generated/PRINT_A3_SYSTEM_SOFTWARE_HARDWARE_INTERFACES.pdf` | Print **two copies**; bring **A3 or larger** for individual viva |
| **3. Definitive group source** | [STM32CubeIDE project root](../Core/Src/demo_app.c) + GitHub commit SHA | Flash operating firmware before exam; keep the same source revision for all members |
| **4. Schematic + BOM** | [Original KiCad project](../Hardware/README.md) | Open KiCad with actual dependency libraries resolved, export verified BOM and revision |
| **5. Raw test evidence** | [`EVIDENCE_REGISTER.csv`](EVIDENCE_REGISTER.csv) | Run physical tests, attach date, MCU board, firmware SHA, threshold, instrument and observed result |

## Assessment weight and exact conditions (rubric pp. 1–6)

- **System Demonstration:** 15% course grade; coherent integrated product, reliable black-box tests, external observable output, no serial/debugger needed. Suggested 10–20 minutes (absolute maximum 30 minutes).
- **Mechanical prototype:** 5%; secured PCB, accessible connectors, practical sound apertures, clean human-centred enclosure. HD includes fit/finish, routing, labels, ergonomics, light pipes and purposeful mechanical additions.
- **Individual viva:** 20%; one randomly chosen question in each of four areas: system/interfaces, code, hypothetical troubleshooting, and reflection. Around five minutes per area. Be able to show your own code and demonstrate general knowledge of teammates' interfaces.
- **Total:** 40% of the course grade. The appointment may reserve 90 minutes for contingencies; this does **not** mean a 90-minute live test.

## Exceeds expectations is conditional on the earlier columns

| Must be demonstrated first | Potential HD distinction | How to demonstrate honestly |
|---|---|---|
| ALL retained requirements, not only an MVP | Highly optimized product | Measure latency, output response, stable CPU headroom and ergonomic usability; don't label unmeasured thresholds as PASS |
| Reliable operation without reset or reflashing | Robustness and rapid recovery | Disconnect permitted source, change control order, vary sound distance; show external output continues or recovers |
| Appropriate safety behaviour | Self-protection | Test bounded output with calibrated fixture; potentially damaging tests **last** and only with lecturer permission |
| Working user system | Classification/adaptive operation/noise rejection | Existing G5 loudness observer is a 3-bin **heuristic**, not trained TinyML; demonstrate actual automatic gain changes, not fictitious classifier labels |

## Current blockers you must resolve before claiming HD

1. **Physical target mismatch:** Nucleo PC13/PA5 control/LED != custom PCB. Confirm printed-board pin mapping, clocks, headers, connectors and user controls.
2. **Microphone:** G5 does not integrate DFSDM ambient microphone or synchronous dual-channel capture; FUN-07 and PER-03 are unverified.
3. **Battery/wear:** no demonstrated two-cell charging, >=24 h discharge, power/current measurement, or actual MCU low-power transition. Source FSM M3 idle != STOP mode.
4. **Audio:** <50 ms acoustic latency, THD <3%, SNR/feedback performance, 20 Hz–20 kHz output, calibrated maximum SPL need measured external evidence.
5. **Mechanics and safety:** assembled headband/enclosure/BOM, fit/finish, protection, environmental evidence and applicability of IEC 60118-13 still require review.
6. **Build:** no ARM compilation or actual unit soak test has been recorded here; host CI only covers its listed checks.

## Regenerate PDFs, run checks

```bash
python3 Assessment/validate_pack.py
python3 -m pip install 'reportlab>=4,<5'
python3 Assessment/generate_pack.py
```

GitHub Actions builds downloadable PDF artifacts from these sources. The generated PDFs are *draft outputs*, not an automated assertion that the device satisfies the requirements.

For oral preparation read [Viva navigation and sample answer structures](VIVA_PREPARATION.md) and [Mechanical acceptance guide](MECHANICAL_ACCEPTANCE.md).

## One-command G7 preparation

From the G1 repository root, run **powershell -ExecutionPolicy Bypass -File .\G1_RUN_ALL.ps1** to create a timestamped evidence ZIP. The [G7 quality gate guide](../Documentation/G7_ONE_COMMAND_QUALITY_GATE.md) explains exact dependencies and remaining ARM and hardware gates. Two-way test-case traceability and its mutation tests are automated. The strict physical evidence audit intentionally stays blocked until all 13 external tests have documented, signed passing observations.

The G7 evidence audit has [host mutation tests](test_evidence_audit.py): blank/false PASS entries, invalid results, corrupted SHA identifiers and missing test rows cannot satisfy its integrity gate. The [Windows ZIP smoke job](../.github/workflows/verify-g3.yml) is source-only and explicitly skips C compilation and PDF creation; it is not an ARM or hardware demonstration.
