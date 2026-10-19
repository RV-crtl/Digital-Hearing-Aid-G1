# Public-source publication and product-release gate

**Current classification: PRIVATE educational engineering prototype.** The source repository can be polished and reviewed for eventual publication; it is **not** a complete commercial, medical, acoustic-safety-validated or custom-PCB-ready product.

## Distinguish three approvals

| Gate | Evidence required | Current state |
| --- | --- | --- |
| Source/host engineering | Reproducible revision, CI (GCC + Clang + sanitizers), documentation, no high-confidence tracked secrets, reviewable change history | Automated; inspect newest run before merging |
| Permission to make *repository public* | Written permission from all contributors/rightsholders and institution as applicable; clearance for each supplied PDF, group KiCad/prototype, and third-party asset; deliberate project-wide `LICENSE`; privacy review | **NOT CLEARED** — no project-wide `LICENSE` and approvals not recorded |
| Physical device or market/clinical release | Exact-board ARM build + binary provenance, full electrical/power/battery tests, calibrated acoustic safety and performance, enclosure, robustness, regulatory pathway relevant to market and product claims | **NOT CLEARED** — current firmware targets NUCLEO-L476RG; custom PCB and acoustic measurements pending |

These gates are independent: green software CI permits a source-code review, **not** publicity rights, a product release or safe use by a person.

## Required human publication review

- [ ] Obtain written agreement from each group member/source contributor and the relevant institution. Verify copyright assignments, assessment and coursework disclosure rules.
- [ ] Agree on one explicit project licence (or intentional source-available terms) and add a project-wide `LICENSE` **only when authorised by all rights-holders**. Existing ST/CMSIS component licences do not automatically licence the team's code or board.
- [ ] Manually review `Documentation/Reference/*.pdf`, `References/GroupMemberPrototypes/`, `Hardware/Original_KiCad/`, dashboard assets, images and documentation for ownership, confidentiality, embedded metadata and personal information. Remove/rewrite anything not cleared **before** changing visibility. Removing from HEAD does not scrub git history.
- [ ] Preserve and review ST HAL and CMSIS licence texts; inventory every external library, board footprint, font and reference asset. See [third-party provenance](THIRD_PARTY_PROVENANCE.md).
- [ ] Review **entire git history** for credentials, PII or restricted artefacts. The fast `Tools/publication_audit.py` only checks the current tracked tree and known token patterns. Use repository-native secret scanning, a dedicated tool such as Gitleaks, and manual review in addition.
- [ ] Decide whether to release a *clean export repository* rather than change the existing historical team-research repository to public. Never expose private speech recordings or participant measurements.
- [ ] Re-run the host tests, inspect the ARM compilation + complete flash/map evidence, verify physical revision/pin wiring, and execute the signed `Documentation/RELEASE_CHECKLIST.md` before using 'finished product' language.
- [ ] Review `SECURITY.md`, configure private vulnerability reporting, access controls, branch protection and a maintenance/update plan appropriate to the chosen visibility.

## Checks and reproducibility

Run `python3 Tools/publication_audit.py --output Validation/results/publication_audit.json` to verify *tracked-tree hygiene only*. The script **always** reports `public_release_ready: false` until human clearance; it does not attempt to attest consent. Run `bash Validation/run_host_validation.sh` and inspect GitHub Actions for the same commit. Use `G1_RUN_ALL.ps1 -BuildArm` on a Windows machine with supported STM32CubeIDE and FW_L4 middleware; it does not flash, validate acoustics or adapt pins for the custom PCB.

For final release, record the tested Git SHA, checksum of exact flashed ELF/HEX, board serial/revision, supplier versions, acceptance thresholds, raw power/acoustic traces, operators, date and signed decision in a controlled evidence register. Include a rollback procedure.

**Visibility is not changed by this document or CI.** There is no automatic public release, commercial distribution permission or product certification.
