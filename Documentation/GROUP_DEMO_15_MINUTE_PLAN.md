> **Firmware QR261104.2:** See [`../README_QUIET_RIOT_DEMO_FINAL.md`](../README_QUIET_RIOT_DEMO_FINAL.md) for the updated operational controls and safety limits.

# Group demonstration — 15–18 minute live test plan

This is an **editable rehearsal plan**, not a claim that tests have passed. It follows the group rubric: full integration, **observable black-box behaviour**, feasible 10–20 min sequence (max 30), operational without ST-Link or debug UART, on intended battery supply. Print two copies of the updated system/block diagram. Revise the group's numeric targets and submit the combined requirements/test-plan PDF to Moodle as the rubric requires.

| Window | Requirement / action (physical, observed) | Observable pass criterion | Evidence to collect |
|---|---|---|---|
| 0:00–1:30 | Show complete custom PCB, two LiFePO4 cells, power-up without USB, explain power rails and I2S/DFSDM buses | Boot independently; state cue heard; no unsafe heating | Short live video, power voltage measurement |
| 1:30–4:00 | E11 microphone speech at two distances; vary volume using encoder | Voice audible without harsh clipping; volume changes predictably | External audio recording, visual volume response |
| 4:00–6:30 | E8 BT; pair and play known spoken audio/1-kHz reference | BT link cue (when module confirms link); continuous audible playback | Pairing display, speaker recording |
| 6:30–9:00 | E9 TC; switch transmitter off/on, check speech or known test signal | LOW24 reception gives audible wanted signal; report residual hum openly | Test generator setting, recording/noise floor |
| 9:00–11:00 | E11→E8→E9→E11 rapidly; E10 mute/unmute | All modes respond, no permanent TC lock-up; confirmation cues occur | Record control sequence and timing |
| 11:00–12:30 | E8 held 1 s: ROOM on/off in BT; E11 held 1 s: optional mic rumble filter on/off | Reproducible effect change; default path returns cleanly | Before/after external listening, option state |
| 12:30–14:30 | Unexpected order: disconnect Bluetooth; switch away/back, source muted/unmuted; power-cycle | No software latch, responsive physical controls, valid recovery | Observe board behaviour (not just console) |
| 14:30–18:00 | FreeRTOS state/task/ISR explanation, DSP, safety limiter and known telecoil limitation; Q&A | Explain scheduling, DMA, memory ownership, no synthetic tasks in operating image | Print architecture, tested code and evidence register |

**Do not demonstrate a proprietary surround algorithm or true ANC as implemented**: current hardware cannot support such claims. If TC hum remains above speech, do not hide it with a gate. Show the actual limitation and existing calibrated bypass. Group HD/exceeds-expectations requires robust, safe and reliable external operation—more features alone do not earn that mark.

## Test setup: no PC-dependent behaviour

- Flash in advance, disconnect ST-Link and serial, and power using the intended, correctly protected LiFePO4 pack.
- Use a **bench-mounted speaker/actuator** first, not an in-ear load, when measuring the percussive feedback gain or maximum volume.
- Have a known source with voice/500 Hz/1 kHz signals and a repeatable induction transmitter. Avoid claiming a numeric SNR if it was not measured.
- Bring printed revised system diagram and detailed requirements/test plan, the complete code, test evidence and an accessible recovery firmware image.
