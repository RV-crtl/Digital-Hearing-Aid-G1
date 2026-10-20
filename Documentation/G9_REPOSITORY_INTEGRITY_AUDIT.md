# G9 — full repository folder integrity and build-evidence checks

**Scope:** G1 source repository only, including root scripts, `Core/`, `Recovery/`, `Dashboard/`, `Documentation/`, `Assessment/`, `Tools/`, `Validation/`, GitHub workflows and tracked `Hardware/` paths. The PD5 repository is not modified.

## Automatically checked

| Area | Gate | What it establishes |
| --- | --- | --- |
| Every tracked `*.md` path | `Validation/python/check_tree_integrity.py` and mutation tests | Relative file links resolve and filenames do not collide on case-insensitive Windows |
| Portable C firmware modules | `Validation/run_host_validation.py` and `.sh` | GCC/Clang compile **16 HAL-independent source modules**, including less frequently exercised telecoil, connectivity and signal-quality modules; portable behaviour is tested separately |
| CubeIDE/project/mirror contracts | `Tools/g1_preflight.py`, `CHECK_PROJECT.py`, `CHECK_GROUP.py` | Selected source/recovery mirrors, disabled operating stress load, task handoff and project metadata remain consistent |
| PowerShell scripts | Windows GitHub Actions | Every top-level `*.ps1` parses; the source-mode ZIP runner executes |
| ZIP evidence | `Tools/verify_evidence_bundle.py` and mutation tests | ZIP file list and every artifact hash match the manifest; tampering, extra files, unsafe paths and fabricated hardware-pass claims are rejected |
| Local ARM build report | `Tools/verify_arm_evidence.py` and mutation tests | A **locally generated**, current-run report matches firmware revision, expected Debug/Release location, ELF signature, file size and SHA-256 |
| Assessment and evidence | `Assessment/validate_pack.py`, `Assessment/evidence_audit.py` | Requirement/test consistency and no unsupported physical PASS claims; not a physical test |
| Original KiCad | `Validation/python/audit_kicad.py` | Tracked design files and table dependencies are inventoried; **not** a real electrical-rule/design-rule check |

### Run

On Windows, from the G1 root:

```powershell
powershell -ExecutionPolicy Bypass -File .\G1_RUN_ALL.ps1
python Tools/verify_evidence_bundle.py .\G1_QUALITY_EVIDENCE_<timestamp>.zip
```

Use the actual produced ZIP filename. To attempt a local ARM Clean/Build after installing STM32CubeIDE and the STM32Cube FW_L4 V1.18.x dependency, add `-BuildArm`. The G1 runner then discards stale ARM reports, confirms the report was produced by this run, and checks the produced ELF against its SHA-256 and source revision. It never flashes the board.

On Linux/macOS or in GitHub Actions:

```bash
bash Validation/run_host_validation.sh
python3 Validation/run_host_validation.py
```

## Known blockers that cannot be repaired from source-only CI

- **KiCad dependencies:** 50 referenced external symbol/footprint library URIs remain unavailable from this repository. Reacquire the original team-supplied source libraries, clear their distribution rights, and validate them with KiCad. The original design is not fabrication-ready simply because source checks pass. See `Hardware/README.md` and the `Validation/results/kicad_audit.json` generated locally.
- **Local ARM build:** FreeRTOS vendor sources are intentionally not tracked, so a clean checkout is not an independently compiled Cortex-M4 firmware binary. Install the supported STM32Cube FW_L4 version, run `PREPARE_PROJECT.bat`, then run an actual CubeIDE build and save the logs. The GitHub Windows job verifies PowerShell syntax and a *host-only* bundle, not the target firmware.
- **Board/product:** Custom PCB pin and rail validation, microphone integration, Bluetooth/telecoil physical playback, acoustic level and distortion limits, enclosure, safe power/charging and endurance need calibrated bench evidence. Never claim a certified hearing aid or finished consumer device from CI results alone.
- **Public release:** Group/institution licence clearance, historical privacy review and third-party rights remain open in issue #11. Repository visibility must remain private until authorised.

**Limitations:** whole-tree Markdown validation checks relative target paths, not website availability, heading anchors or document meaning. Portable C compilation is not a replacement for target-specific HAL/RTOS compilation. A source audit cannot verify a physical medical device.
