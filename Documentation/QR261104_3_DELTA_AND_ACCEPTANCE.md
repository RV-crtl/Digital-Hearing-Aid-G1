# Quiet Riot QR261104.3 — Focused Repair and Build Acceptance

## Source changes

- User-requested `AT+CA100\r\n` on every BT401 connection edge (`TS+01` / `TS+02`) following disconnection; periodic link-status query every 3 s and reconnect-edge unit test.
- The BT401 public module guide lists host volume settings from **0 to 30**. `CA100` is therefore a *requested experiment*, **not** verified handset 100% remote volume. `AT+QA` is queried to inspect its actual effect.
- Event-driven cues are stronger, with a separate BT-disconnect cue. Their amplitude is still subject to transducer bandwidth and electrical output measurement.
- Physical pad mappings and sleeps remain unchanged.
- Tone/chirp bench generator supports 32,200 PCM peak (98.3% 16-bit digital full scale) with interpolated sine and fade. Normal listening gain cap is untouched. Digital peak does not equal electrical/acoustic output power.
- TC decoder remains LOW24 with high-sensitivity recovery, exact-rate playback and stable channel selection; high-pass/low-pass filtering was only gently adjusted to target out-of-band noise, with **no new gate** or automatic notch chasing. For unknown periodic noise, capture raw input and use `TC HUM LEARN` *with external transmitter off*; an unmeasured interferer cannot be guaranteed removed.
- Exactly one visible `main.c` and one `.ioc`. Recovery `main.c.golden` is intentionally non-compilable; project prepare script restores it and keeps ISR/DMA source parity.

## Build/test evidence

Portable PCM/audio/control regressions, official project and memory guards, 80/80 ARM Clang compile checks and Clang/LLD relocatable-link smoke all passed. Native Windows CubeIDE ARM GNU/Newlib executable **not** available in this environment; no ELF or acoustic test is claimed. Use `PREPARE_PROJECT.bat`, `VERIFY_FINAL.bat` and CubeIDE **Project > Clean** then **Build**. Do not flash unless the linker finishes successfully.

## Demo acceptance (bench fixture; not on head)

1. Confirm E8/E9/E10/E11 and E8+E11 sleep/proximity wake behave identically to QR261104.2.
2. Connect phone -> inspect BT status/QA; disconnect -> reconnect and confirm CA100 transmits again; verify phone slider separately (firmware cannot guarantee it).
3. Exercise source MIC/BT/TC, mute/unmute and observe audible connection/disconnection cues. Verify DAC no clipping under cue mix.
4. With the external TC induction transmitter OFF, compare ambient noise in TC at low speaker volume. If a stable tone remains, use bench diagnostic build to run `TC HUM LEARN`, inspect `TC HUM STATUS`, `TC PROBE` and an actual `RECORD TC` capture; then compare `TC HUM OFF/ON`. If the remaining noise is broadband, hardware-origin or coincident with programme, escalate to analogue grounding/ADC analysis rather than more blind gating.
5. Test 1 kHz/chirp on resistor/transducer fixture with amplifier gain initially reduced; increase only after measuring electrical and acoustic output bounds.

## Changed tracked files
- `CHECK_PROJECT.py`
- `Core/Inc/dha_feedback.h`
- `Core/Inc/qr_demo_test.h`
- `Core/Src/bt_profile.c`
- `Core/Src/demo_app.c`
- `Core/Src/dha_feedback.c`
- `Core/Src/qr_demo_test.c`
- `Core/Src/tc_cleanup.c`
- `Dashboard/__pycache__/connectivity_dashboard.cpython-313.pyc`
- `PREPARE_PROJECT.ps1`
- `README_QUIET_RIOT_DEMO_FINAL.md`
- `Recovery/Core/Inc/dha_feedback.h`
- `Recovery/Core/Inc/qr_demo_test.h`
- `Recovery/Core/Src/demo_app.c`
- `Recovery/Core/Src/dha_feedback.c`
- `Recovery/Core/Src/qr_demo_test.c`
- `Recovery/Core/Src/tc_cleanup.c`
- `Tests/test_qr_runtime.c`
- `Tools/test_qr_demo_release.py`
- `Tools/verify_261104_release.py`

## Added
- `Core/Inc/qr_bt_volume.h`
- `Core/Src/qr_bt_volume.c`
- `README_QUIET_RIOT_QR261104_3.md`
- `Recovery/Core/Inc/qr_bt_volume.h`
- `Recovery/Core/Src/bt_profile.c`
- `Recovery/Core/Src/main.c.golden`
- `Recovery/Core/Src/qr_bt_volume.c`
- `Tests/test_qr_bt_volume.c`

## Removed
- `Recovery/261104-Digital_Hearing_Aid.ioc`
- `Recovery/Core/Src/main.c`
