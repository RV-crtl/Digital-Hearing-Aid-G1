#include <assert.h>
#include <stdio.h>
#include "g1_user_controls.h"
#include "group_encoder.h"
int main(void)
{
  G1UiState s; G1Ui_Init(&s);
  assert(s.source==G1_UI_MIC && !s.muted && s.gain_q15==4096U);
  assert(G1Ui_Touch(&s,1U<<12)==0U); /* proximity is NOT a button */
  assert(G1Ui_Touch(&s,1U<<0)==0U);  /* E0..E7 inactive */
  assert((G1Ui_Touch(&s,1U<<8)&G1_UI_SOURCE_CHANGED) && s.source==G1_UI_BT);
  assert((G1Ui_Touch(&s,1U<<9)&G1_UI_SOURCE_CHANGED) && s.source==G1_UI_TC);
  assert((G1Ui_Touch(&s,1U<<10)&G1_UI_MUTE_CHANGED) && s.muted);
  assert((G1Ui_Touch(&s,1U<<10)&G1_UI_MUTE_CHANGED) && !s.muted);
  assert((G1Ui_Touch(&s,1U<<11)&G1_UI_SOURCE_CHANGED) && s.source==G1_UI_MIC);
  assert(G1Ui_Encoder(&s,2)==G1_UI_GAIN_CHANGED && s.gain_q15==5120U);
  assert((G1Ui_Command(&s,"VOLUME 100\r\n")&G1_UI_GAIN_CHANGED) &&
         s.gain_q15==G1_UI_MAX_GAIN_Q15);
  assert(G1Ui_Command(&s,"VOLUME 101")==G1_UI_INVALID);
  assert(G1Ui_Command(&s,"VOLUME 99999999999999")==G1_UI_INVALID);
  GroupEncoder g; GroupEncoder_Init(&g,0,0); assert(g.transitions_per_step==4U);
  assert(GroupEncoder_Update(&g,0,1)==0);
  assert(GroupEncoder_Update(&g,1,1)==0);
  assert(GroupEncoder_Update(&g,1,0)==0);
  assert(GroupEncoder_Update(&g,0,0)==1);
  GroupEncoder_Init(&g,0,0); g.transitions_per_step=2U;
  assert(GroupEncoder_Update(&g,0,1)==0);
  assert(GroupEncoder_Update(&g,1,1)==1);
  puts("R20 host controls PASS");
  return 0;
}
