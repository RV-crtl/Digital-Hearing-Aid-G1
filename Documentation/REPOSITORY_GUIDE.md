# G1 contributor and repository guide

**Start with the [README](../README.md)**, not old Nucleo firmware screenshots. This is the 03 November 2026 custom-PCB project, with a separately tested R24 baseline and an R26 DSP/ML candidate.

| Task | Read | Verify |
| --- | --- | --- |
| Understand the physical system | [A3 architecture](PRINTABLE_SYSTEM_DIAGRAM.md) and [current requirements](G1_CURRENT_SYSTEM_REQUIREMENTS.md) | Actual board, original KiCad files and correct peripheral ownership |
| Navigate the code | [Code map](CODE_MAP.md) and [R26 performance](R26_OPTIMISATION_NOTES.md) | Do not treat `Core/` legacy root as the full custom-board R26 image |
| Find source designs | [Original KiCad](../Hardware/Original_KiCad), [CAD guide](CAD_ASSEMBLY_AND_VIEWERS.md) | Retain source; exported views are not ERC/DRC |
| Test the whole system | [Black-box plan](BLACK_BOX_TEST_PLAN.md) | Run untethered, on safe output fixture, record real results |
| Work on the website | [Site README](../docs/README.md) and `docs/` | `python docs/validate_site.py`, `python docs/test_render_pcb.py` |
| Update the latest full-body 3MF | `Tools/import_latest_3mf.py` and [CAD provenance](CAD_ASSEMBLY_AND_VIEWERS.md) | Inspect/owner-authorize/commit manually; Pages GLB regeneration |
| Work on DSP | `Core/Src/g1_ambient_dsp.c`, `g1_scene_ml.c`, `tc_cleanup.c` | R26 sanitizer tests and host-only benchmark, then target tests |
| Make any changes to CubeMX | `PREPARE_PROJECT.ps1`, `Recovery/` | Regenerate, restore recovery mirror, clean native GCC build |

## Source of truth

- **Original schematics/layout** live in `Hardware/Original_KiCad/`. They have **not changed** with newer 3MF models.
- **Hosted website CAD** is an earlier design revision until the new 1,713-object 3MF is explicitly imported and committed.
- **R24 custom-PCB** is the last hardware version the operator reported fully working untethered after a wiring correction.
- **R26** is a later, source-tested candidate with advanced audio and online unsupervised clustering; physical acoustic/CPU evidence is pending.
- **Older Nucleo G5/G11 source files** stay as auditable history, not the product operating configuration.

There is no certified electroacoustic safety result or blanket public-use licence. Consult [publication gate](PUBLICATION_GATE.md).

## Contribution quality gates

Run `python3 Validation/run_host_validation.py`, `python3 docs/validate_site.py` and the DSP tests if touched. Capture native ARM CubeIDE build results before lab deployment. Keep changes scoped: never simultaneously rewrite clock, DMA ownership, audio routing and adaptive gain in an untested flash image. Prefer small testable commits and evidence at the same Git SHA.
