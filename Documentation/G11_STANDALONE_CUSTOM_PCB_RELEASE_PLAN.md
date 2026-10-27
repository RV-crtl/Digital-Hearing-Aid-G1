# G11 · Standalone custom-PCB demonstration and systems integration

## Contract / target configuration

The assessed operating product is the **custom hearing-aid PCB**, self-powered by **two series-connected 26650 LiFePO4 cells**, each specified by the team at **3.25 V nominal** (therefore **6.50 V nominal pack**). No NUCLEO board, ST-LINK, serial console or computer is required or permitted as a hidden part of normal operating mode. A Bluetooth source such as a phone may be used only where it is part of the system requirement. The repository is public; these operating facts do not imply the associated code has already been built and run on the custom PCB.

**Status definition:** the team reports functioning physical hardware. This repo contains NUCLEO-targeted code paths (notably PC13 B1 and PA5 LD2) and the custom PCB's separate U13 pin map. Consequently **physical operation reported by the team** is not the same as verified **custom PCB integration for the current repository SHA**. No synthetic pass marks. The only credible claim of custom PCB firmware integration is a reproduced clean ARM build for the exact board config and a black-box observed demonstration.

## Actual input/output buses and firmware hand-off

| Path | Transport / type | Ownership | Status and next integration test |
|---|---|---|---|
| Bluetooth source | External module, serial-audio SAI1A and control UART | BT peripheral DMA ISR publishes RX block, BtTask services UART, AudioTask owns audio and source-control state | Portable and host-level interface tests exist; repeat real paired-phone test on custom PCB |
| Telecoil | Pickup + analogue front end → SAI2A RX words | DMA ISR publishes completed half-buffer; AudioTask decodes and arbitrates | Test with a real induction loop and output transducer (no electrical injection substitute for final black-box test) |
| Microphone | PDM digital microphone → DFSDM clock/data → decimated PCM | **To be owned by microphone driver, without blocking AudioTask** | U13 PCB pad 10 = DFSDM1_CKOUT; pad 35 = DFSDM1_DATIN2. **DFSDM microphone capture is not wired to active firmware in `Core/Src/demo_app.c`** |
| Output | 16-bit mono PCM FIFO → stereo output slots → SAI2B TX DMA → amplifier | AudioTask single producer, SAI2B DMA interrupt consumer | Check actual external transducer at calibrated load, clipping and measured delay |
| User commands | Physical interface → bounded queue `GroupEvent` | Physical input producer; AudioTask sole owner of profile/volume state | Current `GROUP_NUCLEO_B1_ENABLED` path uses NUCLEO PC13/PA5; rewrite and verify using actual PCB controls |
| Adaptive settings | Observed amplitude/spectral context + explicit user actions | `adaptive_preferences.c` is a tested portable library only | **Not yet connected** to hardware sampling, physical controls, stored preference state or audio processing; do not claim on-device learning at the demonstration |

The source currently has a completed RX-DMA snapshot before `vTaskNotifyGiveFromISR`, an AudioTask consuming notifications with a bounded 5-ms idle wait, and explicit `g_bt_dma_words`, `g_tc_dma_words`, `g_demo_processed_mono`, and `g_speaker_tx_dma` symbols in `Core/Src/demo_app.c`. The system diagram should distinguish *data arrows* (audio buffers / PCM) from *control arrows* (queue messages / flags) and *power arrows* (pack → regulation → rails).

## Power and safety engineering acceptance

Never connect 6.5 V nominal directly to MCU supply rails. Confirm the actual schematic traces the pack, reverse-polarity protection, fuse/current protection, 2S charge/balance/protection approach and regulators. The actual **allowed cell voltage window**, regulator dropout, undervoltage action, peak load, thermals and battery life must be based on selected cells/components and measurements, not inferred from nominal 3.25 V. Verify any charging feature explicitly: a USB-C connector does not prove safe 2-cell charging. Stress/supply tests with potential damage should happen **last** in the demonstration.

If adaptive processing is enabled, it must respect a **separately measured, hard output limit** even when the user has repeatedly requested increased volume; firmware preference bounds are not substitutes for acoustic safety measurements.

## Rubric-aligned assessor pack (source: System Demonstration and Viva, pp. 1–7)

- Print **two** A3 system diagrams with both software and actual PCB interconnections, including buses, IRQs, queues and each buffer's owner.
- Provide an agreed **whole-system requirements matrix** with quantitative bounds, explicit test-case mapping, and all currently unmet requirements marked BLOCKED, not PASS.
- Demonstration **10–20 minutes**, absolute maximum **30 minutes**. Test no hidden ST-LINK, debugger, serial UART or PC; demonstrate intended two-cell power system as naturally used.
- Use direct **black-box observations** for system requirements. A scope debugger or internal DMA signal cannot replace observed audio behaviour. For latency, THD and supply current, measure with calibrated instruments and record test conditions.
- Expect unfamiliar button order, disconnected cables or power variation. Check recovery, persistent operation, delayed controls and fail-safe behaviour. Leave destructive-stress scenarios until the end.
- Mechanical review: board securely mounted; lid/removal fit, smooth buttons, openings for microphones, access to connectors, protected speaker and sensible cable routing, labels and ergonomics.
- Viva: trace one user action through ISR → queue → task → output; explain each task's priority and peripheral ownership; discuss audio DMA block lifetimes, error handling and overflow/underflow; show original source contributions and real KiCad/BOM.

## Suggested physical black-box sequence (measured, not prefilled)

| Minute | External test | Direct observation / evidence |
|---|---|---|
| 0–2 | Standalone enclosure, controls, 2S pack, no ST-LINK | Start, boot, audio readiness and nominal pack reading; enclosure / battery visual check |
| 2–5 | Bluetooth pair, varied content, user volume/profile action | Sound output, reasonable control responsiveness; no crash or reset |
| 5–8 | Genuine telecoil loop and change source | Audible loop signal and source priority |
| 8–11 | Speak into physical microphone, with other audio sources muted | Acoustic capture and intelligibility. **BLOCKED until PDM/DFSDM integrated** |
| 11–14 | Timed external stimulus, measured output, signal/noise test | Quantitative latency / distortion / response vs explicit requirements |
| 14–17 | Unexpected input order, loss and restoration of signal | Autonomous recovery, bounded volume, no repeated glitches |
| 17–20 | Enclosure inspection, environmental/noise changes, power reading | Mechanical acceptance, measured stable supply and fault log via external observations |

This is a planning template, not evidence that any step passed. Update test prerequisites, exact limits, operator and instrument calibration before use.

## AI integration: recommended closed-loop contract

1. Extract bounded features from real acquired PCM: RMS / log-amplitude, low/mid/high spectral band powers, source identity, optional scene stability and transient indicators; avoid slow FFT in the DMA ISR.
2. Treat physical user gain adjustment as **explicit preference feedback** within a stable scene. Do not interpret louder ambient noise alone as a request for additional gain.
3. Begin with small per-context preferences, slow adaptation, confidence thresholds, hysteresis, and limited total/step-wise gain changes; reset quickly on invalid measurements and source loss.
4. Enforce clipping detection, thermal/power fault protection and the **independent calibrated acoustic limit** after the adaptive recommendation.
5. Use a versioned, wear-limited, checksummed preference storage format; validate power interruption and corrupt storage fallback before implementing nonvolatile persistence.
6. Test under source switching, rapid repeated user adjustments, low battery and misleading/unexpected audio. Keep the ability to disable and clear learned preferences via a physical control.

`Core/Src/adaptive_preferences.c` and `Tests/test_adaptive_preferences.c` establish a bounded, deterministic **portable starting point**. They do not presently configure DFSDM or provide a field-validated learning algorithm.

## Release discipline

A bot is not a hardware test. Require host GCC/Clang/sanitizers, the actual custom-board clean ARM build, sign-off on input/power/output pins, and signed black-box evidence before tagging a physical firmware release. GitHub Pages may present the design but should distinguish *source-tested*, *physical observation* and *concept/proposed* stages.
