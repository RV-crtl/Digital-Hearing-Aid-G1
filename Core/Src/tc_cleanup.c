/** @file tc_cleanup.c
 * @brief R26.6 stable telecoil conditioning with *no* activity-triggered
 * muting, no gain pumping, and no frequency chasing. A user-commanded
 * source-off calibration can set one stationary interference notch.
 * AudioTask owns this state. 48 kSa/s; no allocation or blocking.
 */
#include "tc_cleanup.h"
#include <math.h>
#include <string.h>
#define TC_RATE 48000.0f
#define TC_PI 3.14159265358979323846f
#define TC_PEAK 28000.0f
#define TC_HP_A 0.01600f /* ~123 Hz: mild mains rumble reduction without harming 240Hz/500Hz */
#define TC_LP_A 0.43000f /* gentle dual LP for out-of-band hiss; no gating */
static int16_t clip16(float x,TcCleanup *s){
 if(x>TC_PEAK){s->limiter_samples++;return (int16_t)TC_PEAK;}
 if(x< -TC_PEAK){s->limiter_samples++;return (int16_t)(-TC_PEAK);}
 return (int16_t)x;
}
void TcCleanup_Reset(TcCleanup *st){
 if(!st)return;
 memset(st,0,sizeof(*st));
 st->gate_gain=1.0f;st->signal_gain=1.0f;
 st->notch_b0=1.0f;
}
void TcCleanup_SetNotchAuto(TcCleanup *st,bool enabled){
 if(st)st->notch_auto_enabled=enabled?1U:0U;
}
void TcCleanup_SetNotchFrequency(TcCleanup *st,uint32_t hz){
 if(!st)return;
 if(hz<75U || hz>4500U){
   st->notch_auto_enabled=0U;st->notch_hz=0U;
   return;
 }
 /* Narrow fixed-frequency notch, Q=18; no coefficient updates while
  * program audio is playing, no reset of HP/LP history. */
 const float w=2.0f*TC_PI*(float)hz/TC_RATE;
 const float alpha=sinf(w)/36.0f;
 const float inv=1.0f/(1.0f+alpha);
 st->notch_b0=inv;st->notch_b1=-2.0f*cosf(w)*inv;st->notch_b2=inv;
 st->notch_a1=st->notch_b1;st->notch_a2=(1.0f-alpha)*inv;
 st->notch_x1=st->notch_x2=st->notch_y1=st->notch_y2=0.0f;
 st->notch_mix=0.0f;
 st->notch_hz=hz;st->notch_auto_enabled=1U;
 st->notch_locks++;
}
void TcCleanup_Block(TcCleanup *st,const int16_t *in,int16_t *out,size_t n){
 if(!st || !in || !out || !n)return;
 uint64_t input_energy=0,output_energy=0;
 const float target=(st->notch_auto_enabled && st->notch_hz)?1.0f:0.0f;
 for(size_t i=0;i<n;i++){
    const float x=(float)in[i];
    input_energy+=(uint64_t)((int64_t)in[i]*(int64_t)in[i]);
    st->hp_lp+=TC_HP_A*(x-st->hp_lp);
    const float hp=x-st->hp_lp;
    st->lp+=TC_LP_A*(hp-st->lp);
    st->lp2+=TC_LP_A*(st->lp-st->lp2);
    const float baseline=st->lp2;
    const float notched=st->notch_b0*baseline+st->notch_b1*st->notch_x1+
       st->notch_b2*st->notch_x2-st->notch_a1*st->notch_y1-st->notch_a2*st->notch_y2;
    st->notch_x2=st->notch_x1;st->notch_x1=baseline;
    st->notch_y2=st->notch_y1;st->notch_y1=notched;
    /* Smooth transitions over ~20 ms. No signal-dependent audio cut. */
    st->notch_mix+=0.001f*(target-st->notch_mix);
    const float y=baseline+st->notch_mix*(notched-baseline);
    out[i]=clip16(y,st);
    output_energy+=(uint64_t)((int64_t)out[i]*(int64_t)out[i]);
 }
 st->last_input_rms=(uint32_t)sqrtf((float)input_energy/(float)n);
 st->last_output_rms=(uint32_t)sqrtf((float)output_energy/(float)n);
 st->gate_gain=1.0f;st->signal_gain=1.0f;st->active_blocks++;
}
