#ifndef GROUP_GUARDIAN_H
#define GROUP_GUARDIAN_H
#include <stdint.h>
/* Single-owner, fixed-memory policy. Counts come from existing DMA/audio diagnostics.
 * Level 1 suspends ONLY optional G1 DSP; it never disables the R34 signal path.
 * Wall-clock arithmetic is unsigned, so normal uint32_t tick wrap is supported.
 */
typedef enum { GROUP_GUARD_NORMAL=0, GROUP_GUARD_DEGRADED=1 } GroupGuardLevel;
typedef struct {
    uint32_t previous_fault_count;
    uint32_t last_poll_ms;
    uint32_t last_fault_ms;
    uint32_t events;
    uint32_t recoveries;
    uint8_t level;
    uint8_t initialized;
} GroupGuardian;
void GroupGuardian_Init(GroupGuardian *g);
/* Call at >=100 ms intervals. fault_count is a monotonic sum of overrun,
 * underrun and deadline-miss counters; depth is max capture queue occupancy.
 * Returns current level. AudioTask is the only writer; no heap, no locks.
 */
GroupGuardLevel GroupGuardian_Update(GroupGuardian *g, uint32_t now_ms,
                                      uint32_t fault_count, uint32_t depth);
#endif
