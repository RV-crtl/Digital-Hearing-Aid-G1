# Quiet Riot · QR261104.3 — Focused demo release

This is a full editable STM32CubeIDE project. Import the single project
`261104-Digital_Hearing_Aid` after extracting the ZIP. One active `*.ioc`, one
`Core/Src/main.c`. CubeMX recovery mirrors remain under `Recovery`; its main
snapshot is intentionally `main.c.golden`, not another compilable C file.

## Changes from QR261104.2

* `BtTask` emits the user-requested `AT+CA100` **on each observed BT connection
  transition**, followed by a volume query. It also polls `AT+TS` every three
  seconds to find disconnects/reconnects. The available public BT401 manual
  specifies **CA00..CA30**, so `CA100` support and *phone* slider control are
  **not established**. The command is centralized in `qr_bt_volume.h`.
* Louder original confirmation cues including BT connected/disconnected.
  They remain independent of the normal DSP gain; their SPL is unmeasured.
* 1 kHz and chirp bench tests reach up to 32,200/32,767 digital peak with
  interpolated waveform samples, fade-in/out and a separate software ceiling.
  No claim about maximum physical amplifier power or hearing safety. Never
  use against a head/ear until the transducer limit is measured.
* TC PCM path remains LOW24 full-precision, exact-rate playback with channel
  hysteresis. Only modest stationary speech-band filtering was adjusted;
  never a gate or auto-retuning notch. `TC HUM LEARN` can calibrate one
  narrow, static interferer **while the external induction source is OFF**.
  Unknown in-band noise cannot be guaranteed removed without a raw capture.
* Touch pads, touch gestures, microphone sensitivity, BT audio transport,
  sleep/wake control and speaker DMA routing are preserved.

## Reproducible verification

1. Run `PREPARE_PROJECT.bat` from a Windows terminal; then `VERIFY_FINAL.bat`.
2. Import into STM32CubeIDE 1.19, clean and build. Check for a real ELF and
   zero compiler/linker errors.
3. On a speaker/fixture away from the body, test 1 kHz/chirp at **initially
   low amplifier gain**, then confirm source switching, cues and quiet TC.
4. For noise diagnostics, switch the external induction transmitter OFF while
   keeping TC selected. In bench UART, run `TC HUM LEARN`, then `TC HUM STATUS`,
   `TC PROBE`, and `RECORD TC`/`RECORD DUMP`. Use `TC HUM OFF` to A/B.
5. Confirm phone connect → disconnect → reconnect. Observe UART1 `AT+CA100`
   each time, then `AT+QA` return. Phone slider behaviour is device-specific.

## Verification limitations

Host portable tests and ARM Clang smoke checks are not a native STM32 GNU ELF
link, nor do they validate the PCM1808 wiring, noise spectrum or transducer
output SPL. No in-ear or on-head high-output test is authorized by this package.
