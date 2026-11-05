/* Deterministic host signal tests. Not physical acoustic/ear safety validation. */
#include "g1_ambient_dsp.h"
#include "g1_scene_ml.h"
#include "tc_cleanup.h"
#include "adaptive_preferences.h"
#include <stdint.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include <string.h>
#define N 128U
#define FS 48000.0
#define PI 3.14159265358979323846
static uint32_t rng=0x12AB64E3U;
static float noise(void){rng^=rng<<13;rng^=rng>>17;rng^=rng<<5;return (float)(rng&65535U)/32767.5f-1.0f;}
static double rms(const int16_t *x){double e=0;for(unsigned i=0;i<N;i++)e+=(double)x[i]*x[i];return sqrt(e/N);}
static void tone(int16_t *x,double freq,double amplitude,unsigned block){
 for(unsigned i=0;i<N;i++)x[i]=(int16_t)(amplitude*sin(2*PI*freq*(block*N+i)/FS));
}
static void test_mic(void){
  G1AmbientDsp s;G1Ambient_Init(&s);int16_t in[N],out[N];
  for(unsigned k=0;k<300;k++){tone(in,850.,65.,k);G1Ambient_Process(&s,in,out,N);}
  double faint=rms(out);
  assert(faint>350. && faint<26001.); /* environment and distant speech audible */
  assert(s.gain_q12>32768U && s.gain_q12<=262144U);
  assert(s.learner.trained_blocks==300U);
  assert(s.learner.observations[0]+s.learner.observations[1]+
         s.learner.observations[2]==300U);
  for(unsigned k=0;k<160;k++){tone(in,180.,130.,k);G1Ambient_Process(&s,in,out,N);}
  assert(rms(out)>250.); /* preserve environmental traffic / lower midrange */
  for(unsigned k=0;k<130;k++){
    for(unsigned i=0;i<N;i++)in[i]=(i&1)?25000:-25000;
    G1Ambient_Process(&s,in,out,N);
    for(unsigned i=0;i<N;i++)assert(out[i]>=-26000&&out[i]<=26000);
  }
  /* no hard zero gate: ambient low-level random sound still has variance */
  for(unsigned k=0;k<160;k++){
    for(unsigned i=0;i<N;i++)in[i]=(int16_t)(noise()*24.0f);
    G1Ambient_Process(&s,in,out,N);
  }
  assert(rms(out)>1.0);
  printf("PASS AMBIENT: faint 850 Hz RMS %.1f, bounded AGC and acoustic ML observed %lu blocks\n",
         faint,(unsigned long)s.learner.trained_blocks);
}
static double simulate_tc(double freq,double amplitude,unsigned blocks){
 TcCleanup s;TcCleanup_Reset(&s);int16_t in[N],out[N];double sum=0.;unsigned count=0U;
 for(unsigned k=0;k<blocks;k++){
   tone(in,freq,amplitude,k);TcCleanup_Block(&s,in,out,N);
   if(k>=(blocks/2U)){sum+=rms(out);count++;}
 }
 return sum/count;
}
static void test_tc(void){
  double wanted=simulate_tc(2000.,800.,400);
  double weak=simulate_tc(2000.,260.,400);
  double hiss=simulate_tc(9000.,800.,400);
  /* R26.2 prioritises preservation, not R26.1 hard gating/6x boosting. */
  assert(wanted>420. && wanted<620.);
  assert(weak>130.);
  assert(hiss<wanted*.35);
  TcCleanup st;TcCleanup_Reset(&st);int16_t in[N],out[N];
  for(unsigned k=0;k<500;k++){
    for(unsigned i=0;i<N;i++)in[i]=(int16_t)(noise()*260.f);
    TcCleanup_Block(&st,in,out,N);
  }
  const double idle=rms(out);
  assert(st.gate_gain==1.0f);
  assert(idle>5. && idle<200.); /* no signal-destroying noise gate */
  for(unsigned k=0;k<130;k++){tone(in,2000.,800.,k);TcCleanup_Block(&st,in,out,N);}
  assert(rms(out)>420.); /* wanted tone comes through continuously */
  printf("PASS TC (non-gating): 2k=%.1f weak=%.1f 9k=%.1f idle-hiss=%.1f\n",
         wanted,weak,hiss,idle);
}
static void test_learning(void){
 G1SceneLearner m;G1Scene_Init(&m);
 uint16_t old=m.centre[1][0];
 for(unsigned i=0;i<160;i++)(void)G1Scene_Observe(&m,145U,75U,62U);
 assert(m.trained_blocks==160U);
 assert(m.observations[1]>150U);
 assert(m.centre[1][0]==old);
 assert(m.centre[1][1]>72U); /* online centroid has adapted to actual input */
 G1AdaptivePreferences f;G1Pref_Init(&f);
 G1Pref_Observe(&f,G1_PREF_QUIET,G1_PREF_LOW);
 assert(G1Pref_Get(&f,G1_PREF_QUIET,G1_PREF_LOW)==0);
 G1Pref_Feedback(&f,G1_PREF_QUIET,G1_PREF_LOW,G1_PREF_INCREASE);
 assert(G1Pref_Get(&f,G1_PREF_QUIET,G1_PREF_LOW)>0);
 printf("PASS ML: prototype adaptation + explicit-only preference updates\n");
}
int main(void){test_mic();test_tc();test_learning();puts("R26.2 audio/learning regression PASS");return 0;}
