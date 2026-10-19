# System demo rubric checklist

A PR affecting user interaction, audio source handling, firmware startup, priority, safety or mechanics MUST complete the fields below. CI alone does not satisfy the black-box acceptance criteria.

## Revision and interfaces
- Firmware SHA, target board, supplies, peripherals impacted:
- Task/ISR ownership and FreeRTOS primitives impacted:
- Data type, PCM rates, buffering and callback deadlines:
- Changes to source FSM or physical controls:

## Verification
- [ ] C host verification + MATLAB mathematical reference where relevant
- [ ] ARM CubeIDE Debug and Release build on chosen MCU
- [ ] Measured externally observable effect without ST-LINK/UART
- [ ] Unexpected order/source removal/recovery tested
- [ ] Required quantitative measurements logged with calibrated instrument
- [ ] No production dummy or stress-test tasks
- [ ] Requirement coverage and black-box test updated
- [ ] Printed hardware/software system diagram updated
- [ ] User input / connector / mechanical interface tested physically

## Evidence and limitations
- Actual measurements, units and artifact links:
- Unverified requirement IDs and reason:
- Rollback/contingency:
