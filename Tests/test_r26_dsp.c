#include "g1_ambient_dsp.h"
#include "g1_scene_ml.h"
#include "tc_cleanup.h"
#include <assert.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#define FRAMES 128
static int16_t in[FRAMES],out[FRAMES];
int main(void){
 G1AmbientDsp a;G1Ambient_Init(&a);
 G1SceneLearner m;G1Scene_Init(&m);
 for(int n=0;n<256;n++)(void)G1Scene_Observe(&m,80,20,50);
 assert(m.trained_blocks==256 && m.observations[m.last_cluster]>0);
 for(int b=0;b<180;b++){
  for(int i=0;i<FRAMES;i++) in[i]=(int16_t)(180.0*sin(6.283185307179586*(850.0/48000.0)*(i+b*FRAMES)));
  G1Ambient_Process(&a,in,out,FRAMES);
 }
 assert(a.blocks==180 && a.output_rms>a.input_rms*2U);
 for(int i=0;i<FRAMES;i++)assert(abs(out[i])<=26000);
 TcCleanup tc;TcCleanup_Reset(&tc);
 for(int b=0;b<150;b++){
  for(int i=0;i<FRAMES;i++) in[i]=(i%2)?-200:200;
  TcCleanup_Block(&tc,in,out,FRAMES);
 }
 assert(tc.gate_gain<0.15f);
 for(int b=0;b<180;b++){
  for(int i=0;i<FRAMES;i++)in[i]=(int16_t)(800.0*sin(6.283185307179586*(2000.0/48000.0)*(i+b*FRAMES)));
  TcCleanup_Block(&tc,in,out,FRAMES);
 }
 assert(tc.active_blocks>0 && tc.last_output_rms>tc.last_input_rms);
 for(int i=0;i<FRAMES;i++)assert(abs(out[i])<=27000);
 puts("R26 bounded ambient AGC, online clustering, TC noise-control tests PASS");
 return 0;
}
