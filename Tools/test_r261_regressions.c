/* Retained R26.1 regression coverage, adapted for R26.2 non-gating telecoil.
 * These are deterministic transfer/logic checks, not a measured PCB SNR. */
#include "g1_ambient_dsp.h"
#include "tc_cleanup.h"
#include <assert.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#define FS 48000.0
#define N 128U
#define PI 3.1415926535897932384626433832795
static double rms(const int16_t *x){double e=0.;for(unsigned i=0;i<N;i++)e+=(double)x[i]*x[i];return sqrt(e/N);}
static void make_tone(int16_t *x,unsigned b,double f1,double a1,double f2,double a2){
  for(unsigned i=0;i<N;i++) {
    double t=(double)(b*N+i)/FS;
    x[i]=(int16_t)lrint(a1*sin(2.0*PI*f1*t)+a2*sin(2.0*PI*f2*t));
  }
}
static void test_quiet_mic(void){
  G1AmbientDsp st;G1Ambient_Init(&st);
  int16_t in[N],out[N];
  for(unsigned b=0;b<300;b++){
    make_tone(in,b,850.0,3.0,0.0,0.0);
    G1Ambient_Process(&st,in,out,N);
  }
  double result=rms(out);
  assert(st.gain_q12>200000U);
  assert(result>80. && result<450.);
  for(unsigned b=0;b<180;b++){
    make_tone(in,b,850.0,0.0,0.0,0.0);
    G1Ambient_Process(&st,in,out,N);
  }
  assert(rms(out)==0.0);
  assert(st.gain_q12<4300U);
  for(unsigned b=0;b<12;b++){
    make_tone(in,b,1000.0,29000.0,0.0,0.0);
    G1Ambient_Process(&st,in,out,N);
    for(unsigned i=0;i<N;i++)assert(out[i]>=-26000 && out[i]<=26000);
  }
  printf("PASS mic: 3-count 850Hz input %.1f RMS, silence remains silent, sudden loud bounded\n",result);
}
static void test_stable_low_hum(void){
  TcCleanup st;TcCleanup_Reset(&st);int16_t in[N],out[N];
  TcCleanup_SetNotchFrequency(&st,240U); /* R26.6 deliberate quiet-reference result */
  for(unsigned b=0;b<550;b++){
    make_tone(in,b,240.0,850.0,0.0,0.0);
    TcCleanup_Block(&st,in,out,N);
  }
  double val=rms(out);
  assert(st.notch_hz==240U); /* R26.6 NEVER chases input automatically */
  assert(st.gate_gain==1.0f); /* the old voice-cutting gate must NEVER close */
  assert(st.notch_hz>225U && st.notch_hz<260U);
  assert(st.notch_mix>0.9f);
  assert(val<120.0);
  printf("PASS tc: stationary 240Hz notch: out %.2f RMS, notch %luHz\n",val,(unsigned long)st.notch_hz);
}
static void test_1k_preservation(void){
  TcCleanup st;TcCleanup_Reset(&st);int16_t in[N],out[N];
  for(unsigned b=0;b<400;b++){
    make_tone(in,b,1000.0,400.0,0.0,0.0);
    TcCleanup_Block(&st,in,out,N);
  }
  double val=rms(out);
  assert(st.notch_hz==0U);
  assert(val>230.0);
  printf("PASS tc: 1kHz input 400-peak -> output %.1f RMS, notch disabled\n",val);
}
static void test_500hz_preservation(void){
  TcCleanup st;TcCleanup_Reset(&st);int16_t in[N],out[N];
  for(unsigned b=0;b<400;b++){
    make_tone(in,b,500.0,550.0,0.0,0.0);
    TcCleanup_Block(&st,in,out,N);
  }
  assert(st.notch_hz==0U);
  assert(rms(out)>300.0);
  printf("PASS tc: continuous 500 Hz remains audible %.1f RMS\n",rms(out));
}
static void test_1k_after_hum(void){
  TcCleanup st;TcCleanup_Reset(&st);int16_t in[N],out[N];
  for(unsigned b=0;b<350;b++){
    make_tone(in,b,240.0,850.0,0.0,0.0);
    TcCleanup_Block(&st,in,out,N);
  }
  for(unsigned b=350;b<650;b++){
    make_tone(in,b,240.0,850.0,1000.0,600.0);
    TcCleanup_Block(&st,in,out,N);
  }
  double val=rms(out);
  assert(val>280.0); /* 1k is present continuously over the hum */
  printf("PASS tc: add 1kHz after stationary hum -> output %.1f RMS\n",val);
}
int main(void){test_quiet_mic();test_stable_low_hum();test_1k_preservation();test_500hz_preservation();test_1k_after_hum();puts("R26.2 backwards-compatibility acoustic regressions PASS");return 0;}
