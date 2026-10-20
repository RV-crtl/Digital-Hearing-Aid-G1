# G7: reproducible G1 quality and HD assessment workflow

**Scope: RV-crtl/Digital-Hearing-Aid-G1 only.** The separate individual PD5 repository is not accessed or changed. The normal G1 image defaults to DHA_ENABLE_RTOS_STRESS=0: diagnostic stress workers are not created in the operating image.

## One command on Windows

From the G1 root in PowerShell, run:

    powershell -ExecutionPolicy Bypass -File .\G1_RUN_ALL.ps1

This executes the two-way requirements test-plan audit, mutation tests, recovery mirror and operating-mode preflight, physical evidence-record audit, and compiled portable production-C regressions. It builds the **draft** combined A4 test-plan PDF and an A3 architecture diagram. Finally it generates a timestamped G1_QUALITY_EVIDENCE_*.zip containing the evidence and SHA-256 manifest. The archive includes relative subfolders to identify original evidence sources.

Python 3 and a native GCC/Clang host compiler are required. PDFs are generated with ReportLab installed in a local, Git-ignored .g1-tools-venv, not a global Python installation. With a missing host compiler you can run -SkipPortableTests to produce an explicitly partial report; with an offline Python environment run -SkipPdf. The -RequireArmReady option fails if an ARM compiler or required FreeRTOS source cannot be located.

## Optional automated STM32CubeIDE ARM build on Windows

After installing STM32CubeIDE and the STM32Cube FW_L4 V1.18.x middleware, run the separate G1_BUILD_ARM.ps1 script. It executes PREPARE_PROJECT.ps1, then an STM32CubeIDE headless Debug Clean/Build in an isolated workspace, records compiler logs, verifies the ARM ELF header, and saves a hash-and-build manifest. It never flashes firmware. Close STM32CubeIDE before running. Override -CubeIDEExe or -Workspace if auto-detection fails.

You can append -BuildArm to the normal G1_RUN_ALL.ps1 command to run the headless firmware build after host checks and include ARM build logs and the ELF hash in the evidence ZIP. This option requires a working local ST toolchain, and cannot be exercised by portable GitHub CI. ST's [STM32CubeIDE user guide](https://www.st.com/resource/en/user_manual/um2609-stm32cubeide-user-guide-stmicroelectronics.pdf) documents the headless build interface.

## ARM build and hardware are separate gates

The FreeRTOS vendor tree is *intentionally not committed*. A fresh G1 clone is not a self-contained ARM build: install STM32Cube FW_L4 V1.18.x, close CubeIDE, run PREPARE_PROJECT.bat and require FINAL PREPARE PASS; then Clean/Build in CubeIDE and examine actual compiler, linker, ELF/map and flash logs. The source preflight cannot prove Cortex-M4 compilation.

For NUCLEO-L476RG, the B1/LD2 board pins are PC13/PA5. The supplied custom PCB is separately unverified and its user-control mapping is not equivalent. Do not claim the product is custom-board ready.

## Cross-platform host route

    python3 Assessment/validate_pack.py
    python3 Assessment/test_validate_pack.py
    python3 Tools/g1_preflight.py
    python3 Assessment/evidence_audit.py
    python3 Validation/run_host_validation.py
    python3 -m pip install 'reportlab>=4,<5'
    python3 Assessment/generate_pack.py

Run Assessment/evidence_audit.py --strict **only for final release certification of the evidence file**. It intentionally fails until every test case has a measured, complete signed PASS record. Empty fields remain NOT_RUN; they are not counted as passing.

## Acceptance boundaries

| Automated result | What is genuinely demonstrated | Still unverified |
| --- | --- | --- |
| Portable C under GCC/Clang, independent filter vectors | Algorithm regressions against host test inputs | ARM timing, actual SAI/DMA, acoustic latency |
| Bidirectional coverage and mutation tests | Test IDs agree in both source tables; invalid mapping fails | Whether physical acceptance thresholds are realistic |
| Source/Recovery preflight | Restored files, intended build flags and reported dependencies | Compiled, flashed operating ELF |
| A4/A3 PDF generation | Repeatable assessment print material | Group approval and device performance |
| Evidence register audit | Unsupported or incomplete PASS reports fail source audit | Honesty/accuracy of physical measurements |

The **final course demonstration** is external black-box testing: aim for 10–20 minutes, not more than 30; show the audio system operating on intended power with ST-Link and serial debugger disconnected; print two system diagrams; submit one revised requirements/test-plan PDF. Physically damaging-condition tests come last and require a safe, appropriate measurement fixture.

**Open physical blockers:** DFSDM microphone(s), feedback cancellation, hearing-fit state and genuine low-power transition, battery endurance, custom PCB electrical/controls port, safe acoustic output, THD, latency and mechanical fit. Treat these as unverified until measured. A green GitHub check is not an HD grade or a clinical safety certification.

## G9 integrity hardening (20 October 2026)

The full host test pipeline now audits every tracked Markdown relative link and rejects case-insensitive filename collisions that would affect Windows checkouts. The tests also mutate the ARM report to prove that stale runs, wrong firmware revisions, wrong build folders, tampered executables, and incorrect flashing claims are rejected. These checks run under GitHub Actions and require no physical hardware.

When `G1_RUN_ALL.ps1 -BuildArm` is used, the script first clears any old ARM report, builds using the installed local CubeIDE/FW_L4, and then verifies the new report with `Tools/verify_arm_evidence.py`. It verifies the source commit, current-run timestamp, built executable's ARM ELF signature, size and SHA-256 before setting the manifest's ARM-build flag. The ELF and local hardware still require their own engineering review; source-only CI does not build the ARM project. An unknown commit (e.g. when using an exported ZIP without Git history) is recorded as `UNKNOWN`, not silently converted into a verified Git provenance.
