/* R26.2 portable C integration tests. No physical-board, PCM1808 or SAI proof. */
#include "tc_cleanup.h"
#include "g1_user_controls.h"
#include "pcm_transport.h"
#include <assert.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#define N 128U
#define PI 3.14159265358979323846
static void sample(int16_t *x,unsigned block,double f,double a,double f2,double a2){
  for(unsigned i=0;i<N;i++){
    double t=(block*N+i)/48000.0;
    x[i]=(int16_t)lrint(a*sin(2.0*PI*f*t)+a2*sin(2.0*PI*f2*t));
  }
}
static double rms(const int16_t *x){double e=0;for(unsigned i=0;i<N;i++)e+=(double)x[i]*x[i];return sqrt(e/N);}
static void test_mode_switch(void){
 G1UiState s;G1Ui_Init(&s);
 assert(s.source==G1_UI_MIC);
 uint32_t c=G1Ui_Touch(&s,1U<<9);assert((c&G1_UI_SOURCE_CHANGED)&&s.source==G1_UI_TC);
 c=G1Ui_Touch(&s,1U<<11);assert((c&G1_UI_SOURCE_CHANGED)&&s.source==G1_UI_MIC);
 c=G1Ui_Touch(&s,1U<<8);assert((c&G1_UI_SOURCE_CHANGED)&&s.source==G1_UI_BT);
 c=G1Ui_Touch(&s,1U<<9);assert((c&G1_UI_SOURCE_CHANGED)&&s.source==G1_UI_TC);
 c=G1Ui_Touch(&s,1U<<10);assert((c&G1_UI_MUTE_CHANGED)&&s.muted);
 c=G1Ui_Touch(&s,1U<<11);assert((c&G1_UI_SOURCE_CHANGED)&&s.source==G1_UI_MIC&&s.muted);
 c=G1Ui_Touch(&s,1U<<10);assert((c&G1_UI_MUTE_CHANGED)&&!s.muted);
 puts("PASS controls: TC->MIC->BT->TC->MIC, mute retained until user toggle");
}
static void test_legacy_pcm_lane(void){
 const uint32_t raw[4]={0x000003F0U,0x000004A0U,0x0000FE81U,0x0000FF80U};
 int16_t left[2]={0},right[2]={0};
 assert(PcmTransport_DecodeStereoWordsMode(raw,2,left,right,PCM_DECODE_LOW16)==PCM_TRANSPORT_OK);
 assert(left[0]==1008&&right[0]==1184&&left[1]==-383&&right[1]==-128);
 assert(PcmTransport_DecodeWordMode(0x0000FF80U,PCM_DECODE_LOW24)!=-128);
 puts("PASS PCM: preserve measured LOW16 signed lane, avoid speculative 24-bit shift");
}
static void test_undistorted_speech(void){
 TcCleanup st;TcCleanup_Reset(&st);int16_t in[N],out[N];double r=0;
 for(unsigned b=0;b<500;b++){
   sample(in,b,1000.,400.,0.,0.);TcCleanup_Block(&st,in,out,N);
   if(b>450)r+=rms(out);
 }
 r/=49.;
 assert(r>255.&&r<320.);
 assert(st.notch_hz==0U && st.gate_gain==1.f);
 printf("PASS speech: steady 1kHz RMS %.1f, no notch, no gate\n",r);
}
static void test_learned_hum_plus_speech(void){
 TcCleanup st;TcCleanup_Reset(&st);int16_t in[N],out[N];
 TcCleanup_SetNotchFrequency(&st,240U); /* learned from a separate source-off capture */
 for(unsigned b=0;b<420;b++){
   sample(in,b,240.,850.,0.,0.);TcCleanup_Block(&st,in,out,N);
 }
 assert(st.notch_hz>=225U && st.notch_hz<=250U);
 double projection=0, denom=0;
 for(unsigned b=420;b<620;b++){
   sample(in,b,240.,850.,1000.,600.);TcCleanup_Block(&st,in,out,N);
   if(b>=520)for(unsigned i=0;i<N;i++){
     const double test=sin(2*PI*1000.*(b*N+i)/48000.0);
     projection+=(double)out[i]*test;denom+=test*test;
   }
 }
 const double amplitude=projection/denom;
 assert(amplitude>470.&&amplitude<650.);
 assert(st.gate_gain==1.f);
 printf("PASS mixed signal: 240 Hz hum locked, 1kHz preserved amplitude %.1f, notch %lu Hz\n",amplitude,(unsigned long)st.notch_hz);
 TcCleanup_SetNotchAuto(&st,0);
 for(unsigned b=620;b<680;b++){
   sample(in,b,240.,850.,0.,0.);TcCleanup_Block(&st,in,out,N);
 }
 assert(st.notch_mix<0.05f && rms(out)>400.);
 puts("PASS bypass: TC NOTCH OFF fades notch without muting voice bandwidth");
}
int main(void){test_mode_switch();test_legacy_pcm_lane();test_undistorted_speech();test_learned_hum_plus_speech();puts("R26.2 PORTABLE REGRESSIONS PASS");return 0;}
