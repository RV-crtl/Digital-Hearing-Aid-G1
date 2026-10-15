# Group G1: additional firmware functionality — prototype baseline

## Implemented in this repository

- Existing **R34** Bluetooth/telecoil direct playback and production M1-M4
  connectivity controller, speaker DMA, DSP and three CPU profiler paths.
- New `group_features.c`: **four listening profiles**, click-resistant gain
  ramps, reused existing compressor/EQ settings in routed mode, optional
  low-frequency wind/high-pass effect, finite-state automatic
  *loudness* observer (quiet/moderate/loud only), and safe integer clipping.
- New `xQueueCreate(8, sizeof(GroupEvent))`: nonblocking cross-task settings
  events from ControlTask to the single AudioTask owner. No new scheduler.
- Nucleo B1 (PC13) input and LD2 (PA5) indicator, configured at runtime.
  Short release -> next profile; long release (>=1.2 s) -> auto-level on/off.
  LED solid when group processing on in manual mode; blink 100ms per second
  during auto-level; off when group processing disabled.
- New portable quadrature decoder `group_encoder.c` for future PCB PA0/PA1
  integration; **not enabled in Nucleo target**. TIM2 profiler remains free.
- Existing `!RS`, `!RD` and `!RH` demonstrations retained for TESTING ONLY.

## Controls and expected feedback

Default group DSP is **OFF** so original audio is not changed before hardware tests.
Use Nucleo B1 short press to enable it and select the next profile, or use:

| Command | Effect |
|---|---|
| `!HI` | Confirms `group=G1_ADAPTIVE_BASE`, R34 identity retained |
| `!GP` | Real group enabled, automatic, profile, scene, gain, sample-count telemetry |
| `!GON` | Activate additional profile DSP |
| `!GOFF` | Bypass group DSP (original source still routed) |
| `!G0` | Neutral / unity gain |
| `!G1` | Voice / modest attenuation |
| `!G2` | Music / dynamic-preserving unity gain |
| `!G3` | Wind / first-order low-frequency attenuation and lower gain |
| `!GA` | Automatic, **deterministic loudness protection**, not ML |
| `!GM` | Disable auto profile adjustment |

Queued commands are applied in AudioTask. The CLI `ACK` means **enqueued**;
`!GP` confirms the applied effect. No PC connection is required to use B1.
This is a **Nucleo feature**; PC13 is unconnected on the custom PCB.

The loudness observer uses inexpensive mean-absolute-value and hysteresis.
It does **not** recognise speech, music, location or wind reliably, and must
never be advertised as TinyML. Accurate scene classification needs labelled
acoustic training data and a separate deploy/test workflow.

## RTOS priorities and queue architecture

- AudioTask priority 7 owns all DSP state (including group features).
- ControlTask priority 6 handles USB-STLINK-UART commands and Nucleo B1 polling.
- Bluetooth supervision priority 5, dual background stress priority 2,
  optional high-priority demonstration-only load 7, plus FreeRTOS kernel tasks.
- Group queue is **nonblocking**, protects AudioTask state from concurrent
  modification, and maintains ordering of profile requests.
- TIM2 1-MHz CPU profiling, TIM6 HAL tick and three profiler implementations
  are UNCHANGED. Do not edit FreeRTOSConfig.h or tasks.c optimisation.
- Existing AudioTask is still partly polling; event-driven DMA notifications
  are a **recommended next integration phase**, but replacing it blindly would
  risk the working audio transport. This package does NOT claim that fix.

## Not finished / not yet verified

- Complete DFSDM microphone acquisition merged into R34: **not implemented**;
  separate prototype source exists, pin/DMA conflict audit still required.
- MPR121 capacitive touch and PA0/PA1 encoder controls **on the custom PCB**:
  no group PCB target configuration, hardware pin validation or input testing.
- TinyML: no labelled field training set or validated embedded model supplied.
- Multiband fitted WDRC and adaptive beamforming: require calibration;
  not silently advertised as working. Existing DSP compressor/EQ retained.
- USB-C device modes, battery charger, battery balancing, physical power
  fault sensing, acoustic output calibration: **not implemented or verified**.
- No full ARM build, physical board flash, RTOS CPU instrumentation check
  or audio quality test has been performed in this environment.

## Product-safety warning

Do not use as a medical hearing device. PCM clipping/limiting is not a
calibrated safe SPL ceiling. Start with minimal gain and verify amplifier
and transducer output levels externally.
