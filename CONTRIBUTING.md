# Contributing and branch policy

This repository combines contributions from multiple students; do not alter the individual PD5 repository when working on the group project.

1. Create a feature branch named `feature/<short-name>`, `fix/<short-name>` or `validation/<short-name>`.
2. Keep CubeIDE project metadata (`.project`, `.cproject`, `.ioc`) at repository root. Treat `Core`, `Recovery`, generated HAL, and FreeRTOSConfig changes as high risk.
3. Make a small module change with a matching portable test. Add the feature's actual status and evidence method to `Documentation/REQUIREMENTS_EVIDENCE.csv`.
4. Run `bash Validation/run_host_validation.sh`. If editing the MCU scheduler/audio path, also perform the full CubeIDE build and hardware regression before proposing merge.
5. Raise a pull request using the template. Describe what changed, measured before/after behavior, pin/clock effects, limitations and rollback instructions.
6. Seek appropriate group review for source and hardware; do not merge unverified custom-board peripheral changes onto `main`.

Never commit private keys, BLE pairing secrets, GitHub tokens, ST-LINK serial identifiers, compiled `Debug/` or `Release/` files, laboratory recordings containing private speech, or large vendor libraries without license review. Third-party design libraries must keep source licenses and provenance.

`Recovery/` mirrors are not independent implementations; if modifying protected generated code, update the matching recovery copy and run `CHECK_PROJECT.py` and `CHECK_GROUP.py`.

**Labels used in PRs:** `host-verified`, `onboard-pending`, `pcb-gate`, `breaking-peripheral`, `documentation`, `experiment-only` (create labels when desired).
