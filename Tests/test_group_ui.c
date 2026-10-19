#include "group_ui.h"
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
static GroupUiGesture step(GroupUiButton *ui,uint32_t *ms,uint8_t pressed,uint32_t duration) {
 GroupUiGesture observed=GROUP_UI_NONE;
 for (uint32_t elapsed=0;elapsed<duration;elapsed+=5U) {
  GroupUiGesture e=GroupUi_Poll(ui,*ms,pressed);
  if (e!=GROUP_UI_NONE) {assert(observed==GROUP_UI_NONE);observed=e;}
  *ms+=5U;
 }
 return observed;
}
static GroupUiGesture click(GroupUiButton *ui,uint32_t *ms) {
 assert(step(ui,ms,1U,80U)==GROUP_UI_NONE);
 return step(ui,ms,0U,75U);
}
int main(void) {
 GroupUiButton ui;uint32_t now=0U;
 GroupUi_Init(&ui);assert(GroupUi_Poll(&ui,now,0U)==GROUP_UI_NONE);
 assert(click(&ui,&now)==GROUP_UI_NONE);
 assert(step(&ui,&now,0U,350U)==GROUP_UI_SINGLE);
 assert(click(&ui,&now)==GROUP_UI_NONE);
 assert(click(&ui,&now)==GROUP_UI_NONE);
 assert(step(&ui,&now,0U,350U)==GROUP_UI_DOUBLE);
 assert(click(&ui,&now)==GROUP_UI_NONE);
 assert(click(&ui,&now)==GROUP_UI_NONE);
 assert(click(&ui,&now)==GROUP_UI_TRIPLE);
 assert(step(&ui,&now,0U,350U)==GROUP_UI_NONE);
 assert(step(&ui,&now,1U,1300U)==GROUP_UI_NONE);
 assert(step(&ui,&now,0U,85U)==GROUP_UI_LONG);
 assert(step(&ui,&now,1U,4200U)==GROUP_UI_NONE);
 assert(step(&ui,&now,0U,85U)==GROUP_UI_RESET);
 assert(step(&ui,&now,1U,20U)==GROUP_UI_NONE);
 assert(step(&ui,&now,0U,400U)==GROUP_UI_NONE);
 GroupUi_Init(&ui);now=UINT32_MAX-180U;GroupUi_Poll(&ui,now,0U);
 assert(click(&ui,&now)==GROUP_UI_NONE);
 assert(step(&ui,&now,0U,350U)==GROUP_UI_SINGLE);
 assert(ui.dispatch_count==1U);
 assert(GroupUi_Poll(NULL,now,1U)==GROUP_UI_NONE);
 puts("GROUP UI: 1/2/3-click, long/reset, bounce, wrap PASS");return 0;
}
