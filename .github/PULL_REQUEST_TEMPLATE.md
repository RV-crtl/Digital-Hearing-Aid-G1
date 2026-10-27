## What changed and why

## Affected target
- [ ] NUCLEO-L476RG (currently imported firmware)
- [ ] Custom group PCB (requires separate pin/clock audit)
- [ ] Pure host / MATLAB verification only

## Verification evidence
- [ ] `bash Validation/run_host_validation.sh` passes
- [ ] Matlab figures from actual C vectors attached (if DSP touched)
- [ ] STM32CubeIDE Debug + Release ARM build completed (if embedded firmware touched)
- [ ] On-board playback, source switching, CPU runtime and DMA error counters checked
- [ ] KiCad ERC/DRC rerun (if hardware touched)

**Board revision / firmware SHA / test conditions / raw evidence:**

## Risks and rollback

## Updated requirement rows / documentation

Do not claim medical certification, safety SPL compliance, real TinyML training, functional battery charging or custom-board support without measured evidence.

## G11 standalone PCB changes
- [ ] Final 2S LiFePO4 protection, fuse/current limit and regulator behavior verified on assembled board
- [ ] Real PCB pin assignments compared with actual KiCad/MCU pad map (not just Nucleo)
- [ ] Mic DFSDM capture only described as working if exercised on final hardware
- [ ] Adaptive learning changes include feedback-event and acoustic guardrail evidence
- [ ] A3 system diagram and black-box whole-system requirements/testplan updated
- [ ] Public Pages CAD asset and contributors' rights checked before publishing
