#include "group_guardian.h"
#include <stddef.h>
#define GROUP_GUARD_POLL_MS 100U
#define GROUP_GUARD_RECOVER_MS 3000U
#define GROUP_GUARD_HIGH_WATER 6U /* 8-deep ring uses 7 usable slots */
void GroupGuardian_Init(GroupGuardian *g) {
    if (!g) return;
    *g=(GroupGuardian){0};
}
GroupGuardLevel GroupGuardian_Update(GroupGuardian *g, uint32_t now_ms,
                                      uint32_t fault_count, uint32_t depth) {
    if (!g) return GROUP_GUARD_NORMAL;
    if (!g->initialized) {
        g->previous_fault_count=fault_count;
        g->last_poll_ms=now_ms;
        g->last_fault_ms=now_ms;
        g->initialized=1U;
        return GROUP_GUARD_NORMAL;
    }
    if ((uint32_t)(now_ms-g->last_poll_ms)<GROUP_GUARD_POLL_MS)
        return (GroupGuardLevel)g->level;
    g->last_poll_ms=now_ms;
    /* Counter resets (e.g. manual diagnostics reset) must not be treated as
     * giant unsigned deltas. Genuine rollover at UINT32_MAX is rare here;
     * treating it as a new baseline is safer than false overload. */
    const uint8_t new_error=(uint8_t)(fault_count>g->previous_fault_count);
    g->previous_fault_count=fault_count;
    if (new_error || depth>=GROUP_GUARD_HIGH_WATER) {
        g->last_fault_ms=now_ms;
        if (g->level!=GROUP_GUARD_DEGRADED) {
            g->level=GROUP_GUARD_DEGRADED;
            ++g->events;
        }
    } else if (g->level==GROUP_GUARD_DEGRADED &&
               (uint32_t)(now_ms-g->last_fault_ms)>=GROUP_GUARD_RECOVER_MS) {
        g->level=GROUP_GUARD_NORMAL;
        ++g->recoveries;
    }
    return (GroupGuardLevel)g->level;
}
