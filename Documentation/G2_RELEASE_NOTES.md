# G2 engineering upgrade — staged hardware validation

G2 is an **incremental reliability update** to the G1 Nucleo baseline. It is not a custom-PCB port, USB charger, trained AI model, certified hearing device, or a verified ARM flash build.

## Functional changes

- `GroupGuardian`: low-overhead, AudioTask-owned overload policy. It samples *real existing counters* (speaker underruns, Bluetooth/telecoil capture overruns, service deadline misses) and maximum capture queue occupancy. If counters rise or a capture ring has 6+ of its 7 usable slots occupied, it enters `DEGRADED` and suspends optional G1 processing, leaving the R34 audio transport intact.
- After three seconds without another detected overrun or high-water event, the guardian returns to `NORMAL` and restores the user-requested optional processing. Event and recovery counters are visible in `g_group_telemetry` and `!GP` output.
- The optional profile bypass now correctly copies input to output when the buffers differ; previously it silently left the destination unchanged.
- Expanded the portable DSP tests from 5 to 7 and added guardian tests for startup, poll interval, overload, hold-down, recovery, diagnostic resets and tick wraparound.
- Added Linux GCC regression CI for these portable modules and the original Python project validators.

## RTOS and peripheral constraints

No scheduler, FreeRTOSConfig, task priorities, TIM2/TIM6 profiler, DMA circular buffers, SAI clocks, interrupt priorities, startup, or source-selection FSM algorithms were intentionally changed.

The guardian is intentionally **not a new thread**: AudioTask exclusively owns its state, and its 100 ms polling gate avoids another heap allocation or locking protocol. AudioTask remains the only writer of optional DSP state. The original queue (`xQueueCreate`) still carries control/UI requests. Only noncritical G1 filters are bypassed under fault pressure.

## Commands

`!GP` now includes `guard=0|1`, `guard_events`, and `recoveries`. `guard=1` means optional G1 processing is temporarily bypassed; the configured profile and `enabled` flag remain saved. The `!G...` control commands are unchanged.

## Validation and limitations

Host checks run with standard GCC and `-Wall -Wextra -Werror`. Existing application tests and recovery mirrors remain in place. **No physical hardware test or full ARM cross-compile is claimed.** On-board validation must check both direct playback modes, AUTO routing, Bluetooth loss/recovery, RTOS profiler continuity, pause/resume, and that deliberate queue pressure causes guardian entry/recovery without new glitches. The guardian is NOT a medical audio protection circuit or SPL limiter.

## Group demonstration

Present the hearing-aid functions without a laptop using physically connected audio sources and controls. Keep the high-priority continuous spin task disabled in normal operation; it is a deliberate contention exercise, not a production feature. Do not demonstrate USB charging, PDM capture, battery balancing, trained TinyML or custom-PCB controls as working until each has a physical integration test.
