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
static void test_bypass_distinct_output(void) {
 GroupFeatures f; GroupFeatures_Init(&f);
 int16_t in[]={-32768,-1,0,1,32767}, out[]={8,8,8,8,8};
 GroupFeatures_Process(&f,in,out,5);
 assert(memcmp(in,out,sizeof(in))==0);
 assert(f.processed_blocks==0);
}
static void test_repeated_enable_and_invalid_controls(void) {
 GroupFeatures f; GroupFeatures_Init(&f);
 GroupFeatures_Select(&f,(GroupProfile)250);
 assert(f.profile==GROUP_PROFILE_NEUTRAL);
 GroupFeatures_Enable(&f,1); GroupFeatures_Select(&f,GROUP_PROFILE_WIND);
 int16_t b[4]={32767,-32768,1234,-1234};
 GroupFeatures_Process(&f,b,b,4);
 GroupFeatures_Enable(&f,0);
 int16_t original[4];memcpy(original,b,sizeof(b));
 GroupFeatures_Process(&f,b,b,4); assert(!memcmp(original,b,sizeof(b)));
 GroupFeatures_Enable(&f,1);assert(f.gain_q15==32768U);
}
/* In the normal Nucleo audio chain the buffer is in-place.  At unity
 * the new fast path must not change any sample, telemetry or FSM timing. */
static void test_unity_fast_path_and_observer(void) {
 GroupFeatures f; GroupFeatures_Init(&f); GroupFeatures_Enable(&f,1);
 int16_t src[128], dst[128];
 for (unsigned i=0;i<128;++i) src[i]=(int16_t)(i%2 ? 32767 : -32768);
 for (unsigned k=0;k<32*4;++k) {
   GroupFeatures_Process(&f,src,dst,128);
   assert(memcmp(src,dst,sizeof(src))==0);
 }
 assert(f.processed_blocks==128);
 assert(f.recent_mean_abs>=32767);
 assert(f.scene==GROUP_SCENE_LOUD); /* observer runs in identity path */
 assert(f.saturated_samples==0);
 GroupFeatures_Select(&f,GROUP_PROFILE_MUSIC);
 GroupFeatures_Process(&f,src,dst,128);
 assert(memcmp(src,dst,sizeof(src))==0);
 assert(f.processed_blocks==129);
 /* Switching to an attenuating profile must NOT use the identity shortcut. */
 GroupFeatures_Select(&f,GROUP_PROFILE_VOICE);
 GroupFeatures_Process(&f,src,dst,128);
 assert(f.gain_q15<32768U);
 assert(memcmp(src,dst,sizeof(src))!=0);
}
int main(void) {
 test_off_preserves_pcm();test_profile_and_ramp();test_max_signed_and_no_clipping();
 test_auto_is_not_ml();test_select_manual();
 test_bypass_distinct_output();test_repeated_enable_and_invalid_controls();
 test_unity_fast_path_and_observer();
 puts("GROUP AUDIO PROFILES: 8/8 host tests passed");return 0;
}
