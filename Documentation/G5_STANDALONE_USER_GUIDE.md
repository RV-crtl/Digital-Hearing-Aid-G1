# G5 — Standalone user guide (NUCLEO-L476RG)

> **Target warning:** These controls use Nucleo PC13 (USER B1) and PA5 (LD2). The supplied custom hearing-aid PCB does not route PC13 to a user switch. This is an engineering prototype; audio safety and custom-board electrical operation have not been verified.

## Operate without a laptop

The 5-ms ControlTask polls B1 and feeds `GroupUi_Poll()`. A 45-ms debounce rejects switch bounce; single/double clicks resolve after a 340-ms multi-click window; three clicks dispatch immediately on the third release. A copied `GroupEvent` traverses the RTOS queue; AudioTask alone applies profile and source-control changes.

| Gesture | Effect |
| --- | --- |
| **1 click** | Enable optional DSP and cycle NEUTRAL → VOICE → MUSIC → WIND |
| **2 clicks** | Enable optional DSP and toggle **auto-level** (a three-level loudness heuristic, **not machine learning**) |
| **3 clicks** | Request AUTO → Bluetooth only → telecoil only → AUTO source policy |
| **Hold 1.2–4 s** | Toggle optional DSP on/off (does NOT switch physical power) |
| **Hold ≥4 s** | Reset to neutral profile, optional DSP disabled, auto-level off, AUTO source policy |

**LED:** off = optional DSP off; solid = manual profile; brief pulse once/second = automatic; repeated fast pulses = overloaded optional DSP has been bypassed by the guardian.

The three-click source command requests the **real** FSM mode; it does **not** simulate a Bluetooth pairing status, PCM reception or valid telecoil signal. In legacy direct playback (`!BS`, `!TS`) the selected source is not heard until the FSM is returned to AUTO (`!FA`). Group DSP is optional and normally OFF at boot.

## Development commands (not required at the showcase)

`!GH`: physical-controls help; `!GP`: profile, guardian, count of gestures, source request mode; `!HI`: build tag `G5_STANDALONE_UI_NOTIFY`. These are telemetry, not evidence of a measured sound-pressure level.

## Acceptance test checklist

- Flash a genuine STM32CubeIDE build to the **Nucleo-L476RG**. Confirm source and output power on an instrumented bench with no patient/user.
- Test one, two, three and held presses; verify the gestures are not spuriously repeated by bounce.
- With real Bluetooth and telecoil signals, confirm source-policy requests, mute/loss recovery, and active source (not merely request counter).
- Record DMA callback rates, capture ring high-water marks, speaker underruns and task runtime before and after continuous UI operation.
- Confirm the 20-second priority-7 stress spin remains inactive in normal showcase mode.
- Confirm on-board button/LED pinout independently; never flash Nucleo configuration onto the custom PCB without porting.
- Calibrate acoustic output before claiming physical safety.

## Custom-board blockers

The custom PCB has encoder A/B connected to PA0/PA1 but its button contacts are unconnected, and no user switch exists on PC13. MPR121 address/IRQ and touch wiring need review. DFSDM microphone, charging, USB audio and trained TinyML are not integrated in this firmware. Do not claim these as product capabilities.
