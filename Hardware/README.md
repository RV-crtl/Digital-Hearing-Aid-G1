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

## G11 intended operating supply and standalone target

The team specifies **two 26650 LiFePO4 3.25 V nominal cells in series** (6.50 V nominal). This value is the **battery pack**, not the MCU supply rail: trace the actual cell balancing/protection, current limiting, low-voltage cutoff, buck/LDO conversion and acoustic output stages before operating. The final demonstrator is the custom PCB alone: it must not depend on a Nucleo, debug dongle, PC or serial console. The team reports physical hardware operational, but a specific firmware SHA/PCB revision/end-to-end black-box test record is not attached here. See [G11 custom-PCB bring-up plan](../Documentation/G11_STANDALONE_CUSTOM_PCB_RELEASE_PLAN.md).
