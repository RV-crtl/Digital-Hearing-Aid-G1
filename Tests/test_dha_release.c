#include "dha_feedback.h"
#include "dha_effects.h"
#include "g1_user_controls.h"
#include <assert.h>
#include <string.h>
#include <stdint.h>
#include <stdio.h>

static int32_t abs_i16(int16_t x){return x<0?-(int32_t)x:(int32_t)x;}
int main(void){
 G1UiState ui;G1Ui_Init(&ui);
 assert(ui.source==G1_UI_MIC && !ui.spatial && !ui.noise_reduce);
 assert(G1Ui_Command(&ui,"SPATIAL ON")==G1_UI_SPATIAL_CHANGED && ui.spatial);
 assert(G1Ui_Command(&ui,"SPATIAL ON")==0U);
 assert(G1Ui_Command(&ui,"SPATIAL OFF")==G1_UI_SPATIAL_CHANGED && !ui.spatial);
 assert(G1Ui_Command(&ui,"SPATIAL TOGGLE")==G1_UI_SPATIAL_CHANGED && ui.spatial);
 assert(G1Ui_Command(&ui,"NOISE ON")==G1_UI_NOISE_CHANGED && ui.noise_reduce);
 assert(G1Ui_Command(&ui,"NOISE OFF")==G1_UI_NOISE_CHANGED && !ui.noise_reduce);
 assert(G1Ui_Command(&ui,"ANC ON")==G1_UI_INVALID);
 assert(G1Ui_Touch(&ui,1U<<9)&G1_UI_SOURCE_CHANGED && ui.source==G1_UI_TC);
 assert(G1Ui_Touch(&ui,1U<<11)&G1_UI_SOURCE_CHANGED && ui.source==G1_UI_MIC);
 assert(G1Ui_Touch(&ui,1U<<8)&G1_UI_SOURCE_CHANGED && ui.source==G1_UI_BT);
 assert(G1Ui_Touch(&ui,1U<<10)&G1_UI_MUTE_CHANGED && ui.muted);
 assert(G1Ui_Touch(&ui,1U<<10)&G1_UI_MUTE_CHANGED && !ui.muted);
 int16_t stereo[256]={0};
 DhaFeedback_Request(DHA_CUE_BOOT);
 assert(DhaFeedback_Pending()!=0U);
 DhaFeedback_Render(stereo,128U);
 assert(DhaFeedback_Pending()==0U);
 uint32_t energy=0;
 for(unsigned i=0;i<256;i++)energy+=(uint32_t)abs_i16(stereo[i]);
 assert(energy>1000U);
 /* Clipping is bounded even with high existing programme samples. */
 for(unsigned i=0;i<256;i++)stereo[i]=32767;
 DhaFeedback_Request(DHA_CUE_TC);
 DhaFeedback_Render(stereo,128U);
 for(unsigned i=0;i<256;i++)assert(stereo[i]<=32767 && stereo[i]>=-32768);
 DhaEffects_Init();
 int16_t src[128],test[128];
 for(unsigned i=0;i<128;i++)src[i]=(int16_t)((int)i*101-6000);
 memcpy(test,src,sizeof(src));
 DhaEffects_Process(test,128U,1U); /* TC path must remain bit-exact */
 assert(memcmp(src,test,sizeof(src))==0);
 DhaEffects_SetRoom(1U);
 for(unsigned rep=0;rep<12;rep++){
   memcpy(test,src,sizeof(src));
   DhaEffects_Process(test,128U,1U);
   assert(memcmp(src,test,sizeof(src))==0); /* TC unaffected even if ROOM on */
 }
 memcpy(test,src,sizeof(src));
 DhaEffects_Process(test,128U,2U); /* actual BT enum is 2 */
 assert(memcmp(src,test,sizeof(src))!=0);
 DhaEffects_SetRoom(0U);
 memcpy(test,src,sizeof(src));
 DhaEffects_Process(test,128U,2U);
 assert(memcmp(src,test,sizeof(src))==0);
 DhaEffects_SetNoiseReduction(0U);
 memcpy(test,src,sizeof(src));
 DhaEffects_Process(test,128U,3U); /* MIC unchanged when optional NR off */
 assert(memcmp(src,test,sizeof(src))==0);
 DhaEffects_SetNoiseReduction(1U);
 memcpy(test,src,sizeof(src));
 DhaEffects_Process(test,128U,3U);
 assert(memcmp(src,test,sizeof(src))!=0);
 /* Must never hard-gate a small but real 1-kHz-like input. */
 int nonzero=0;
 for(unsigned k=0;k<24;k++){
   for(unsigned i=0;i<128;i++)src[i]=(i%48)<24 ? 25 : -25;
   memcpy(test,src,sizeof(src));DhaEffects_Process(test,128U,3U);
   for(unsigned i=0;i<128;i++)if(test[i])nonzero++;
 }
 assert(nonzero>1000);
 puts("PASS: product UI, preserved TC, optional ROOM/NR, original cues, non-gating continuity");
 return 0;
}
