# Release acceptance checklist (all gates mandatory for a product claim)

- [ ] Project imports cleanly into supported STM32CubeIDE version without unexpected code regeneration.
- [ ] `PREPARE_PROJECT.bat` passes on a fresh Windows checkout.
- [ ] Debug and Release ARM toolchains compile with zero errors, appropriate FPU ABI, and no undocumented warnings.
- [ ] `bash Validation/run_host_validation.sh` passes (production-C vectors, controller, guardian, profiles, encoder).
- [ ] MATLAB results saved with actual production-C reference CSV, version and coefficients.
- [ ] KiCad third-party library URIs resolved; schematic ERC and PCB DRC pass with saved reports; manufacturer DFM reviewed.
- [ ] Target board variant explicitly selected; pin map, clock source, SAI/DFSDM/DMA and power links measured.
- [ ] Stable external supply and protective overcurrent limits checked; LiFePO4 cell charging/balancing is not assumed.
- [ ] Speaker output muted at startup, safe-mode/fault behavior and correct signal levels measured.
- [ ] Actual Bluetooth, telecoil and microphone signal paths tested independently and under simultaneous stimuli.
- [ ] UI/encoder/touch controls tested physically, including contact bounce and inaccessible switch pins.
- [ ] MCU RTOS task list, CPU time, stack high-water and worst-case audio processing measured on hardware.
- [ ] No queue overflow, speaker underrun, DMA missed deadline or irrecoverable audio stall during timed soak test.
- [ ] Audible/microphone latency, gain response, THD, SNR and acoustic output quantified with calibrated setup.
- [ ] Fault and recovery test run with evidence (real source loss and power cycling); watchdog behaviour verified.
- [ ] Final product enclosure tested for exposed conductive areas, sharp edges, retention, access to controls/USB.
- [ ] Full group demonstration rehearsed with no laptop connection and safe fallback available.
- [ ] Every PASS in the requirements matrix linked to timestamped raw data, board ID, firmware SHA, responsible tester and procedure.

**A passing GitHub Actions workflow is NOT completion of this list.** It proves only the specific host checks executed in CI.
