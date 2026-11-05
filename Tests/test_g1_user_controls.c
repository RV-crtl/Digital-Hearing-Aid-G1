/* Executable Anna E8/E9/E10/E11 product control test, R16. */
#include "g1_user_controls.h"
#include <assert.h>
#include <stdio.h>
int main(void) {
 G1UiState s;G1Ui_Init(&s);
 assert(s.source==G1_UI_MIC);
 assert(G1Ui_Touch(&s,(1U<<8))&G1_UI_SOURCE_CHANGED);assert(s.source==G1_UI_BT);
 assert(G1Ui_Touch(&s,(1U<<9))&G1_UI_SOURCE_CHANGED);assert(s.source==G1_UI_TC);
 assert(G1Ui_Touch(&s,(1U<<10))&G1_UI_MUTE_CHANGED);assert(s.muted);
 assert(G1Ui_Touch(&s,(1U<<10))&G1_UI_MUTE_CHANGED);assert(!s.muted);
 assert(G1Ui_Touch(&s,(1U<<11))&G1_UI_SOURCE_CHANGED);assert(s.source==G1_UI_MIC);
 assert(G1Ui_Touch(&s,0x00FFU)==0U);
 assert(G1Ui_Touch(&s,(1U<<12))==0U);
 assert(G1Ui_Command(&s,"MUTE ON")&G1_UI_MUTE_CHANGED);assert(s.muted);
 assert(G1Ui_Command(&s,"MUTE OFF")&G1_UI_MUTE_CHANGED);assert(!s.muted);
 assert(G1Ui_Command(&s,"SOURCE BT")&G1_UI_SOURCE_CHANGED);assert(s.source==G1_UI_BT);
 assert(G1Ui_Command(&s,"SOURCE MIC")&G1_UI_SOURCE_CHANGED);
 assert(G1Ui_Command(&s,"VOLUME 0")&G1_UI_GAIN_CHANGED);
 assert(G1Ui_Encoder(&s,4)&G1_UI_GAIN_CHANGED);assert(s.gain_q15==4U*G1_UI_STEP_Q15);
 assert(G1Ui_Encoder(&s,-4)&G1_UI_GAIN_CHANGED);assert(s.gain_q15==0);
 (void)G1Ui_Encoder(&s,128);assert(s.gain_q15==G1_UI_MAX_GAIN_Q15);
 assert(G1Ui_Command(&s,"VOLUME 101")==G1_UI_INVALID);
 puts("R16_ANNA_UI_CONTROL_TEST_PASS");return 0;
}
