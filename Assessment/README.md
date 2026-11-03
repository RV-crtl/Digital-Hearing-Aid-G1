# G1 custom PCB — demonstration evidence and reproducible PDFs

**Current source of truth: nine whole-system requirements, seven black-box tests, 20 minutes.**

This folder now matches [current G1 requirements](../Documentation/G1_CURRENT_SYSTEM_REQUIREMENTS.md), the [black-box plan](../Documentation/BLACK_BOX_TEST_PLAN.md), and [printable architecture](../Documentation/PRINTABLE_SYSTEM_DIAGRAM.md). The old G11 24-requirement, 13-test plans are archived under `legacy/` for traceability; they are **not** the present custom-PCB assessment.

## What is generated

| Output | Source | Evidence status |
| --- | --- | --- |
| `generated/SYSTEM_REQUIREMENTS_AND_BLACK_BOX_TEST_PLAN_DRAFT.pdf` | `requirements.csv` + `test_cases.json` | Draft only. Requires group approval and actual black-box observations |
| `generated/PRINT_A3_SYSTEM_SOFTWARE_HARDWARE_INTERFACES.pdf` | `generate_pack.py` | Current real custom PCB and FreeRTOS topology; print two copies on A3 |
| `EVIDENCE_REGISTER.csv` | Seven T1–T7 blank results | **All tests NOT RUN** until actual measurements and sign-off |

The assessment requires a standalone operating state: no ST-LINK, Nucleo, debugging console, synthetic load tasks or unit-test harness running on the device during black-box demonstration. A phone is allowed as the real Bluetooth input. Test external sound/control behaviour rather than relying on register or UART readouts.

## How to build a single final assessment pack

```bash
python3 Assessment/validate_pack.py
python3 Assessment/test_validate_pack.py
python3 Assessment/test_evidence_audit.py
python3 Assessment/evidence_audit.py
python3 Assessment/generate_pack.py --output Assessment/generated
```

Alternatively, run the [Build HD demonstration evidence pack GitHub Action](https://github.com/RV-crtl/Digital-Hearing-Aid-G1/actions/workflows/assessment-pack.yml) and download its generated PDF artifact. **A passing generator is not a PASS on physical requirements.**

## Current hardware and software evidence

R24 was reported to operate on USB-C without Nucleo after resolving an external wiring problem. The operator reported microphone, Bluetooth, MPR121 touch, rotary encoder, speaker and telecoil signed LOW16 playback with its filter enabled. R26 introduces adaptive ambient and telecoil processing, bounded feedback gain and three *unlabelled* scene clusters; its on-board acoustic performance and CPU headroom still require tests. Do not describe R26 clustering as pretrained vehicle or pedestrian recognition. Do not claim battery endurance, safe ear-level SPL or certified hearing correction without measurements.

The correct custom-PCB firmware package must be flashed as one matched CubeIDE project. The repository root is still a historical Nucleo-oriented source baseline with portable DSP integrated, not the complete R26 custom-PCB image. Keep the known-working R24 binary as a rollback.

## Before the assessment

Confirm the supply actually used in the demonstration, firmware commit/build provenance, output safety fixture, 0.5 m and 2–3 m acoustic distances, 2 kHz loop stimulus, and measurable criteria. Fill the evidence register with **real observed** results and retain any failures as failures. Obtain group approval for changes to system-level requirements. Print two A3 diagrams.
