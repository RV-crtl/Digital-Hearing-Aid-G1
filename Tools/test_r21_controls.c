#include "g1_user_controls.h"
#include "group_encoder.h"
#include "adaptive_preferences.h"
#include <assert.h>
#include <stdio.h>
static int rotate(GroupEncoder *e, int dir) {
  static const int c[4][2]={{0,1},{1,1},{1,0},{0,0}};
  static const int a[4][2]={{1,0},{1,1},{0,1},{0,0}};
  const int (*arr)[2]=dir>0?c:a;
  int events=0;
  for(int i=0;i<4;i++)events+=GroupEncoder_Update(e,arr[i][0],arr[i][1]);
  return events;
}
int main(void){
  G1UiState ui;
  G1Ui_Init(&ui);
  assert(ui.source==G1_UI_MIC && ui.muted==0 && ui.adaptive==1);
  assert(G1Ui_Percent(&ui)==100);
  assert(G1Ui_Encoder(&ui,-1)&G1_UI_GAIN_CHANGED);
  assert(G1Ui_Percent(&ui)==95);
  assert(G1Ui_Touch(&ui,1U<<8)&G1_UI_SOURCE_CHANGED);
  assert(ui.source==G1_UI_BT && G1Ui_Percent(&ui)==100);
  assert(G1Ui_Touch(&ui,1U<<10)&G1_UI_MUTE_CHANGED && ui.muted);
  assert((G1Ui_Touch(&ui,1U<<12)&(G1_UI_SOURCE_CHANGED|G1_UI_MUTE_CHANGED))==0);
  assert(G1Ui_Touch(&ui,1U<<9)&G1_UI_SOURCE_CHANGED);
  assert(ui.source==G1_UI_TC);
  assert(G1Ui_Command(&ui,"VOLUME 25\r\n")&G1_UI_GAIN_CHANGED);
  assert(G1Ui_Percent(&ui)==25);
  GroupEncoder e;
  GroupEncoder_Init(&e,0,0); e.transitions_per_step=4;
  assert(GroupEncoder_Update(&e,0,1)==0);
  assert(GroupEncoder_Update(&e,0,0)==0);
  assert(rotate(&e,1)==1);
  assert(rotate(&e,-1)==-1);
  G1AdaptivePreferences learning;
  G1Pref_Init(&learning);
  assert(G1Pref_Observe(&learning,G1_PREF_MODERATE,G1_PREF_MID));
  assert(G1Pref_Feedback(&learning,G1_PREF_MODERATE,G1_PREF_MID,G1_PREF_DECREASE));
  assert(G1Pref_Get(&learning,G1_PREF_MODERATE,G1_PREF_MID)<0);
  assert(G1Pref_Get(&learning,G1_PREF_LOUD,G1_PREF_LOW)==0);
  puts("R21: source, 100%% digital UI cap, 5%% encoder, bounce, proximity, online preference tests PASS");
  return 0;
}
