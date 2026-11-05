#include "qr_demo_test.h"
#include <string.h>
static const int16_t sine48[48] = {
    0,4277,8481,12539,16383,19947,23170,25996,28377,30273,31650,32487,
    32767,32487,31650,30273,28377,25996,23170,19947,16383,12539,8481,4277,
    0,-4277,-8481,-12539,-16383,-19947,-23170,-25996,-28377,-30273,-31650,-32487,
    -32767,-32487,-31650,-30273,-28377,-25996,-23170,-19947,-16384,-12539,-8481,-4277
};
void QrDemoTest_Init(QrDemoTest *s) {if(s)memset(s,0,sizeof(*s));}
uint8_t QrDemoTest_IsActive(const QrDemoTest *s){
 return (uint8_t)(s && (s->gain_q15!=0U || s->playing_mode!=0U));
}
static int16_t test_lookup(uint32_t phase_q16){
 const uint32_t index=(phase_q16>>16)%48U;
 const uint32_t frac=phase_q16&65535U;
 const int32_t a=sine48[index], b=sine48[(index+1U)%48U];
 return (int16_t)(a+(int32_t)(((int64_t)(b-a)*frac)>>16));
}
void QrDemoTest_Render(QrDemoTest *s,uint8_t mode,int16_t *dst,size_t frames){
 if(!s||!dst)return;
 if(mode>4U)mode=0U;
 if(mode!=s->last_mode){
   if(mode){s->phase_q16=0U;s->chirp_sample=0U;s->playing_mode=mode;}
   s->last_mode=mode;
 }
 for(size_t i=0U;i<frames;i++){
   if(mode){
      if(s->gain_q15+QR_TEST_RAMP_INCREMENT_Q15>=32768U)s->gain_q15=32768U;
      else s->gain_q15+=QR_TEST_RAMP_INCREMENT_Q15;
   }else if(s->gain_q15>QR_TEST_RAMP_INCREMENT_Q15)s->gain_q15-=QR_TEST_RAMP_INCREMENT_Q15;
   else {s->gain_q15=0U;s->playing_mode=0U;}
   const uint8_t waveform=mode?mode:s->playing_mode;
   uint32_t step=(uint32_t)waveform<<16;
   if(waveform==4U){
      const uint32_t frequency=250U+(uint32_t)((3750ULL*s->chirp_sample)/QR_TEST_CHIRP_SAMPLES);
      step=(frequency*65536U)/1000U;
      if(++s->chirp_sample>=QR_TEST_CHIRP_SAMPLES)s->chirp_sample=0U;
   }
   s->phase_q16+=step;
   if(s->phase_q16>=48U*65536U)s->phase_q16%=48U*65536U;
   uint32_t envelope=s->gain_q15;
   if(waveform==4U){
      const uint32_t edge=960U;
      if(s->chirp_sample<edge){uint32_t q=s->chirp_sample*32768U/edge;if(q<envelope)envelope=q;}
      if(s->chirp_sample>QR_TEST_CHIRP_SAMPLES-edge){
         uint32_t q=(QR_TEST_CHIRP_SAMPLES-s->chirp_sample)*32768U/edge;
         if(q<envelope)envelope=q;
      }
   }
   int32_t value=((int32_t)test_lookup(s->phase_q16)*QR_TEST_PEAK_PCM)/32767;
   value=(value*(int32_t)envelope)/32768;
   dst[2U*i]=dst[2U*i+1U]=(int16_t)value;
 }
}
