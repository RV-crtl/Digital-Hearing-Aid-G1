#include "group_features.h"
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

static void test_off_preserves_pcm(void) {
 GroupFeatures f; GroupFeatures_Init(&f);
 int16_t s[]={-32768, -500,0,1000,32767}, old[5];
 memcpy(old,s,sizeof(s));
 GroupFeatures_Process(&f,s,s,5);
 assert(!memcmp(old,s,sizeof(s)));
}
static void test_profile_and_ramp(void) {
 GroupFeatures f; GroupFeatures_Init(&f);GroupFeatures_Enable(&f,1);
 GroupFeatures_Select(&f,GROUP_PROFILE_WIND);
 assert(f.profile==GROUP_PROFILE_WIND && f.target_gain_q15==27853);
 int16_t s[128];for(int i=0;i<128;i++)s[i]=(int16_t)(1000 + (i*5));
 GroupFeatures_Process(&f,s,s,128);
 assert(f.gain_q15<32768 && f.gain_q15>=27853);
 assert(f.saturated_samples==0);
 assert(f.recent_mean_abs>0);
}
static void test_max_signed_and_no_clipping(void) {
 GroupFeatures f;GroupFeatures_Init(&f);GroupFeatures_Enable(&f,1);
 int16_t s[]={-32768,32767,-32768,32767};
 GroupFeatures_Process(&f,s,s,4);
 assert(s[0]==-32768 && s[1]==32767);
}
static void test_auto_is_not_ml(void) {
 GroupFeatures f;GroupFeatures_Init(&f);GroupFeatures_Enable(&f,1);
 GroupFeatures_Auto(&f,1);
 int16_t block[128];for(int i=0;i<128;i++)block[i]=15000;
 for(int k=0;k<32*7;k++)GroupFeatures_Process(&f,block,block,128);
 assert(f.scene==GROUP_SCENE_LOUD);
 assert(f.target_gain_q15==27853);
 assert(f.automatic);
}
static void test_select_manual(void) {
 GroupFeatures f;GroupFeatures_Init(&f);GroupFeatures_Auto(&f,1);
 GroupFeatures_Select(&f,GROUP_PROFILE_MUSIC);
 assert(!f.automatic && f.profile==GROUP_PROFILE_MUSIC);
 assert(strcmp(GroupFeatures_ProfileName(2),"MUSIC")==0);
}
int main(void) {
 test_off_preserves_pcm();test_profile_and_ramp();test_max_signed_and_no_clipping();
 test_auto_is_not_ml();test_select_manual();
 puts("GROUP AUDIO PROFILES: 5/5 host tests passed");return 0;
}
