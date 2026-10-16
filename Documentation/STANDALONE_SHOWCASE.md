# Standalone group showcase — no laptop connected to the hearing aid

This is a rehearsal guide, **not a record that physical experiments already passed**. The target is one assembled, protected, battery/externally powered prototype, not a dangling Nucleo/ST-LINK plus PC. Only include segments that have been rehearsed with the exact board, firmware SHA and power source.

## Before audience arrival

1. Check audio output limit with an appropriate test load; begin muted/minimum gain. Never make uncalibrated hearing-safety assertions.
2. Confirm stable 3.3 V / analog rails and a suitable proven power source. USB-C presence does not establish battery charging/balancing.
3. Check each available audio source independently and label equipment/cabling; bring fully charged power source and known-good test signals.
4. Test the *real physical input* that activates any profile changes. Nucleo PC13 USER button is not wired on custom PCB.
5. Confirm normal FreeRTOS operation with the 20-second `!RH` CPU spin **disabled**. Do not run it during end-user audio.
6. Have a prepared fallback: a safe preselected audio input and manually selected filter preset.

## Suggested 10–12 minute sequence

| Approx. time | Presenter and action | What is observable | Evidence limitation |
| --- | --- | --- | --- |
| 0–1 min | Show enclosure, PCB layout and physical connection points | Compact integrated unit; accessible controls | Enclosure evaluation only, not medical certification |
| 1–3 min | Demonstrate one verified audio source into bone conductor | Actual sound passes through MCU/output chain | Claim which source works only after physical check |
| 3–5 min | Demonstrate the other verified source, then source loss/recovery | Mute or source switching rather than a firmware reset | Keep spare source available |
| 5–7 min | Change listening profile on physical control | Audible attenuation/EQ response | UI on custom PCB remains a hardware gate |
| 7–9 min | Explain onboard DSP and playback concurrency | Audio continues while user control changes | RTOS timing requires independent saved measurements |
| 9–11 min | Show original KiCad PCB + documented architecture on static poster | Engineering integration and hard design trade-offs | Do not pretend USB charging/tinyML exists |
| 11–12 min | Revisit test results and known limitations | Evidence-based summary | Carry offline printed/exported MATLAB response figures |

## No laptop means verification evidence must be prepared in advance

Print/save plots from `Validation/matlab/verify_audio_filters.m`, show real KiCad screens or diagrams in slides (presentation display separate from wired prototype if allowed), and keep an offline test register with date/board ID and observations. Do not use animated simulated visuals as proof of physical audio quality.

## Escape hatch

If source selection fails, switch to a previously verified input, keep loudness controlled and explain honestly which interface is not integrated yet. Assessment is stronger when measured limitations are documented than when a staged test is mislabeled as product validation.
