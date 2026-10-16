#include "group_guardian.h"
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
int main(void) {
  GroupGuardian g; GroupGuardian_Init(&g);
  assert(GroupGuardian_Update(&g,0,5,0)==GROUP_GUARD_NORMAL);
  assert(GroupGuardian_Update(&g,50,8,7)==GROUP_GUARD_NORMAL); /* throttle */
  assert(GroupGuardian_Update(&g,100,8,0)==GROUP_GUARD_DEGRADED);
  assert(g.events==1);
  assert(GroupGuardian_Update(&g,250,8,7)==GROUP_GUARD_DEGRADED); /* extend hold */
  assert(GroupGuardian_Update(&g,3200,8,0)==GROUP_GUARD_DEGRADED);
  assert(GroupGuardian_Update(&g,3300,8,0)==GROUP_GUARD_NORMAL);
  assert(g.recoveries==1);
  assert(GroupGuardian_Update(&g,3400,2,0)==GROUP_GUARD_NORMAL); /* counter reset */
  assert(GroupGuardian_Update(&g,3500,2,6)==GROUP_GUARD_DEGRADED);
  assert(g.events==2);
  GroupGuardian_Init(&g);
  assert(GroupGuardian_Update(&g,UINT32_MAX-50,0,0)==GROUP_GUARD_NORMAL);
  assert(GroupGuardian_Update(&g,75,1,0)==GROUP_GUARD_DEGRADED); /* tick wrap */
  puts("GROUP GUARDIAN: startup, errors, queue pressure, recovery, wrap PASS");
  return 0;
}
