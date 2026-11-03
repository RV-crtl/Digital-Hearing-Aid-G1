#include "g1_ambient_dsp.h"
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#define G1_AMBIENT_GAIN_ONE_Q12 4096U
#define G1_AMBIENT_GAIN_MAX_Q12 81920U   /* <= 20x, increased from R24 8x */
#define G1_AMBIENT_PEAK_LIMIT 26000      /* preserves headroom before output DAC */
#define G1_AMBIENT_TARGET_RMS 4300U
#define G1_AMBIENT_MAX_FRAMES 256U

static uint32_t integer_sqrt(uint32_t n){
  uint32_t op=n,res=0U,one=1UL<<30;
  while(one>op)one>>=2;
  while(one){
    if(op>=res+one){op-=res+one;res=(res>>1)+one;}
    else {res>>=1;}
    one>>=2;
  }
  return res;
}
static uint8_t feature_energy(uint32_t rms){
  /* ~log2 scale compressed to one byte; raw amplitude still feeds feedback. */
  unsigned leading=0U;while(rms>1U && leading<15U){rms>>=1;leading++;}
  uint32_t result=(uint32_t)leading*18U;
  return (uint8_t)(result>255U?255U:result);
}
void G1Ambient_Init(G1AmbientDsp *st){
  if(!st)return;
  memset(st,0,sizeof(*st));
  st->gain_q12=G1_AMBIENT_GAIN_ONE_Q12;
  st->ambient_floor_rms=16U;
  G1Scene_Init(&st->learner);
}
void G1Ambient_Process(G1AmbientDsp *st,const int16_t *in,int16_t *out,size_t n){
  if(!st || !in || !out || n==0U || n>G1_AMBIENT_MAX_FRAMES)return;
  /* Low-order DC/sub-bass rejection: fc ~60 Hz, 48 kHz sample rate.
   * One-pole high-pass prevents a DC offset from consuming AGC headroom.
   * No environmental noise gate: weak surroundings remain audible. */
  int16_t filtered[G1_AMBIENT_MAX_FRAMES];
  uint64_t energy=0U;
  uint32_t peak=0U;
  uint16_t zc=0U;
  int32_t prev=0;
  for(size_t i=0;i<n;i++){
    int32_t raw=in[i];
    int32_t delta=(raw*256)-st->dc_lp_q8;
    st->dc_lp_q8+=delta>>7;
    int32_t x=raw-(st->dc_lp_q8>>8);
    if(x>32767)x=32767;
    if(x< -32768)x=-32768;
    filtered[i]=(int16_t)x;
    uint32_t absx=(uint32_t)(x<0?-x:x);
    if(absx>peak)peak=absx;
    energy+=(uint32_t)(x*x);
    if(i>0U && ((x>=0)!=(prev>=0)))zc++;
    prev=x;
  }
  const uint32_t rms=integer_sqrt((uint32_t)(energy/n));
  st->input_rms=rms;st->input_peak=peak;st->zero_crossings=zc;
  st->amplitude_bin=(uint8_t)(rms<150U?0U:rms<2000U?1U:2U);
  st->spectrum_bin=(uint8_t)(zc<8U?0U:zc<32U?1U:2U);
  /* Unsupervised online scene clustering runs independently of waveform DSP. */
  const uint8_t crest=(uint8_t)((peak*32U)/(rms?rms:1U)>255U?
    255U:(peak*32U)/(rms?rms:1U));
  st->scene_cluster=G1Scene_Observe(&st->learner,feature_energy(rms),
     (uint8_t)((zc*2U)>255U?255U:zc*2U),crest);
  /* Floor is telemetry, not a hard gate; increase slowly to avoid learning
   * one sustained speech/traffic event as 'unwanted noise'. */
  if(rms<st->ambient_floor_rms){
    st->ambient_floor_rms=(31U*st->ambient_floor_rms+rms)/32U;
  }else if(rms<st->ambient_floor_rms*2U+50U){
    st->ambient_floor_rms=(255U*st->ambient_floor_rms+rms)/256U;
  }
  if(st->ambient_floor_rms<8U)st->ambient_floor_rms=8U;
  uint32_t desired=G1_AMBIENT_GAIN_ONE_Q12;
  if(rms>=6U){
    uint64_t proposed=(uint64_t)G1_AMBIENT_TARGET_RMS*4096U/rms;
    if(proposed>G1_AMBIENT_GAIN_MAX_Q12)proposed=G1_AMBIENT_GAIN_MAX_Q12;
    if(proposed<2048U)proposed=2048U;
    desired=(uint32_t)proposed;
    if(peak){
      uint32_t safe=(uint32_t)((uint64_t)G1_AMBIENT_PEAK_LIMIT*4096U/peak);
      if(desired>safe)desired=safe;
    }
  }
  /* Reduce gain quickly in response to close loud sounds; raise it gradually
   * so distant speech is more audible without abrupt pumping. */
  if(desired<st->gain_q12)st->gain_q12=(3U*st->gain_q12+desired)/4U;
  else st->gain_q12=(31U*st->gain_q12+desired)/32U;
  uint64_t out_energy=0U;
  for(size_t i=0;i<n;i++){
    int64_t x=((int64_t)filtered[i]*st->gain_q12)/4096;
    if(x>G1_AMBIENT_PEAK_LIMIT){x=G1_AMBIENT_PEAK_LIMIT;st->limited_samples++;}
    if(x< -G1_AMBIENT_PEAK_LIMIT){x=-G1_AMBIENT_PEAK_LIMIT;st->limited_samples++;}
    out[i]=(int16_t)x;
    out_energy+=(uint32_t)(x*x);
  }
  st->output_rms=integer_sqrt((uint32_t)(out_energy/n));
  st->blocks++;
}
