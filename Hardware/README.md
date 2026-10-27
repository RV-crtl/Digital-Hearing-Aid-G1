# KiCad electrical hardware — actual supplied group design

Open [`Original_KiCad/260510-Digital_hearing_aid.kicad_pro`](Original_KiCad/260510-Digital_hearing_aid.kicad_pro) using KiCad's **Project Manager** (rather than separately opening the board and schematics). PCB and hierarchical schematic pages are included. The board is **NOT validated/fabrication-approved** by inclusion in GitHub.

## Critical editing dependency

`fp-lib-table` and `sym-lib-table` reference numerous `${KIPRJMOD}/...` third-party symbol and footprint library folders. Several were supplied in the original group design ZIP but were not copied into this code repository. KiCad can often *view* embedded schematic symbol and PCB footprint representations, but library reassignment and full schematic/PCB editing may fail. Obtain the group's original `260802-Digital_hearing_aid_w_PCB` archive and copy only approved/licensed dependencies into `Hardware/Original_KiCad/`, preserving relative paths. Keep vendor library notices and provenance. Do **not** commit the original archive's `.git`, `.history`, caches, thousands of unrelated components or unreviewed third-party binaries.

Run:

```bash
python3 Validation/python/audit_kicad.py
```

It enumerates unresolved library path references, project/schematic presence and basic PCB object counts. **It does not run KiCad ERC or DRC**.

## Actual required electrical checks

1. Confirm net/pin assignment using [`STM32_U13_PINMAP.csv`](STM32_U13_PINMAP.csv) and the schematic, especially different STM32 peripheral alternate functions compared with the Nucleo firmware.
2. Run KiCad schematic ERC and PCB DRC in a compatible KiCad installation and attach *unedited reports* with the tool version and rule configuration.
3. Verify power input path / source selection, regulated rails, analogue ground, USB-C CC resistors, ESD, debug connector, HSE, BOOT0 and RESET.
4. Confirm the unconnected MPR121 address/IRQ and encoder push contacts. Don't claim these work without design corrections and testing.
5. Check that no 2-cell LiFePO4 charge/balance stage is falsely implied by the USB-C connector.
6. Fabricate only after manufacturer stack-up checks, Gerber review, DFM and real continuity/power-up tests.

For the risk and peripheral porting checklist see [`CUSTOM_PCB_PORTING_CHECKLIST.md`](CUSTOM_PCB_PORTING_CHECKLIST.md).

## G11 intended battery and microphone architecture

The end product is one **custom PCB** with **two 26650 LiFePO₄ cells, 3.25 V nominal each, connected in series (2S, nominal 6.50 V)**. The exact chosen cells and protection/BMS, charge method (if any), balancing, undervoltage, input transients, board regulators and amplifier supply all require a real electrical safety audit. Never treat the GitHub Pages power diagram as approval to apply an unregulated pack directly to 3.3 V MCU pins.

The PCB U13 pin map identifies **DFSDM1_CKOUT (pad 10) and DFSDM1_DATIN2 (pad 35)** for the digital microphone. The checked-in `Core/Src/main.c` does not initialize a DFSDM driver; microphone capture/routing is a firmware integration requirement despite the physical circuit being designed. The provided 3MF is the source-accurate mechanical *design*, not an electrical/fabrication test report.

Refer to [G11 rubric and integration gate](../Documentation/G11_STANDALONE_RUBRIC_AND_ADAPTATION.md) and [standalone black-box test plan](../Assessment/CUSTOM_PCB_BLACK_BOX_TEST_PLAN.md). The hardware owner should attach actual PCB test evidence in issue #2, microphone port status in issue #4 and pack protection data in issue #5.
