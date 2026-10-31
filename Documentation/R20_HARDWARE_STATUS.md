# Custom-PCB firmware status — 31 October 2026

## Baseline separation (important)

The repository's root STM32CubeIDE 1.19.0 project is the older R34/G11 Nucleo-based demonstration plus portable R20 controls merged later. **It is not the full user-tested R20 custom-PCB image.** Do not replace its audio/DMA source piecemeal or represent green portable CI as a physical board release. The separately distributed R20 ZIP is the candidate for controlled whole-project integration; the repository needs an independent clean ARM build and original-hardware tests before promoting it.

## Hardware observations supplied by the operator

| Function | Reported behaviour in R17–R20 series | Repo main-branch validation |
|---|---|---|
| PDM microphone / DFSDM | Working on prototype | Full runtime **not** incorporated |
| Speaker / SAI DMA | Working on prototype | Older Nucleo/SAI baseline only |
| Bluetooth pairing and audio | Working on prototype; right-channel downmix corrected during R16 | Portable tests, not full PCB proof |
| Rotary encoder | Working on prototype | Portable decoder tests pass |
| MPR121 capacitive touch | Working after R17 GPIO-I2C fallback | Control mapping tested; on-board I2C not verified from repo HEAD |
| Telecoil | Not yet demonstrated as working | **Unverified** |
| Battery-only start with ST-LINK physically absent | Reported intermittent failure / incomplete acceptance | **Unverified**; power/NRST/BOOT0 must be checked independently |

## Physical control mapping from Anna's individual demonstration

- E8 — Bluetooth source; E9 — telecoil source; E10 — mute toggle; E11 — microphone source.
- MPR121 proximity bit 12 is presence detection and **not** an E12 button. E0–E7 are inactive in the R20 custom-PCB control profile.
- Rotary encoder controls volume. Mic is the default source at boot in the R20 image.

## Build and safety gates

1. Check MCU model, original PCB pin map, clocks, NVIC, DMA ownership, CubeMX regeneration and reproducible CubeIDE 1.19.0 build; record genuine `.elf`, `.map` and build log.
2. Black-box test battery boot with all ST-LINK leads disconnected, audio on a safe fixture (not in-ear), and touch/encoder changes while powered only from the protected 2S LiFePO4 supply. Verify 3.3 V VDD/VDDA, NRST and BOOT0 if the board depends on ST-LINK.
3. Validate telecoil with an actual induction-loop stimulus; evidence needs changing SAI2A/DMA counters, nonzero signal samples and correctly routed speaker output.
4. Confirm documented release permissions, attribution and safety/quality controls before claiming a finished or open-source medical product.

See Issues #27, #5, #2, #11 for unresolved evidence and power/release prerequisites. This document records observed progress, not clinical, manufacturing or acoustic-safety certification.
