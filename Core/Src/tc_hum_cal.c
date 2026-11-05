/** @file tc_hum_cal.c
 * @brief Capture at 12 kHz and fit a stationary line using Goertzel. Source
 * must be OFF during calibration; content cannot be separated if it has the
 * same exact frequency as the interferer. Work is split across ControlTask
 * iterations, never a heavy audio callback or interrupt.
 */
#include "tc_hum_cal.h"
#include <math.h>
#include <string.h>
#define TC_PI 3.14159265358979323846f
void TcHumCal_Init(TcHumCal *s) { if(s) memset(s,0,sizeof(*s)); }
void TcHumCal_Request(TcHumCal *s) {
 if (!s) return;
 /* Writer ControlTask only; AudioTask captures after it notices request. */
 if(s->state==TC_HUM_IDLE || s->state==TC_HUM_DONE || s->state==TC_HUM_FAILED)
   s->state=TC_HUM_CAPTURE_REQUESTED;
}
void TcHumCal_Feed(TcHumCal *s,const int16_t *samples,size_t n) {
 if(!s || !samples) return;
 if(s->state==TC_HUM_CAPTURE_REQUESTED){
   s->count=s->decim_count=0U;s->decim_accum=0;
   s->sum=s->sum_sq=0;s->state=TC_HUM_CAPTURING;
 }
 if(s->state!=TC_HUM_CAPTURING)return;
 for(size_t i=0;i<n && s->count<TC_HUM_CAL_SAMPLES;i++){
   s->decim_accum+=(int32_t)samples[i];
   if(++s->decim_count==TC_HUM_CAL_DECIMATE){
     int32_t y=s->decim_accum/(int32_t)TC_HUM_CAL_DECIMATE;
     s->pcm[s->count++]=(int16_t)y;
     s->sum+=y; s->sum_sq+=(int64_t)y*y;
     s->decim_accum=0;s->decim_count=0;
   }
 }
 if(s->count==TC_HUM_CAL_SAMPLES){
   const double dc=(double)s->sum/(double)TC_HUM_CAL_SAMPLES;
   double energy=(double)s->sum_sq - (double)TC_HUM_CAL_SAMPLES*dc*dc;
   s->total_power=(float)(energy>0.0?energy:0.0);
   s->bin_index=0;s->bins_measured=0;s->best_coarse_hz=0;
   s->max_coarse_power=s->max_fine_power=s->coarse_sum_power=0.0f;
   s->peak_fraction_permille=0U;s->stage=0;
   s->state=TC_HUM_ANALYSING;
 }
}
/* Windowed signal fit reduces scalloping and false bin selection from leakage.
 * Estimate is purely frequency-domain and never imposes a level gate. */
static float goertzel(const TcHumCal *s,uint32_t hz){
 const float w=2.0f*TC_PI*(float)hz/(float)TC_HUM_CAL_FS;
 const float coeff=2.0f*cosf(w);
 const float dc=(float)s->sum/(float)TC_HUM_CAL_SAMPLES;
 float q1=0.0f,q2=0.0f;
 for(uint32_t i=0;i<TC_HUM_CAL_SAMPLES;i++){
   const float x=(float)s->pcm[i]-dc;
   const float q0=x+coeff*q1-q2;
   q2=q1;q1=q0;
 }
 float pow=q1*q1+q2*q2-coeff*q1*q2;
 return pow>0.0f?pow:0.0f;
}
int TcHumCal_Step(TcHumCal *s){
 if(!s || s->state!=TC_HUM_ANALYSING)return 0;
 /* <~10 PCM16 codes RMS: detector cannot establish a reliable tone. */
 if(s->total_power<(float)(TC_HUM_CAL_SAMPLES*100U)){
    s->state=TC_HUM_FAILED;return 1;
 }
 if(s->stage==0U){
   uint32_t hz=TC_HUM_CAL_START_HZ+s->bin_index*25U;
   if(hz<=TC_HUM_CAL_STOP_HZ){
     float p=goertzel(s,hz);
     s->coarse_sum_power+=p;
     if(p>s->max_coarse_power){s->max_coarse_power=p;s->best_coarse_hz=hz;}
     s->bin_index++;s->bins_measured++;return 0;
   }
   s->stage=1U;s->bin_index=0U;
 }
 int32_t hz=(int32_t)s->best_coarse_hz-24+(int32_t)(s->bin_index*2U);
 if(hz<=((int32_t)s->best_coarse_hz+24)){
   if(hz>=(int32_t)TC_HUM_CAL_START_HZ && hz<=(int32_t)TC_HUM_CAL_STOP_HZ){
     float p=goertzel(s,(uint32_t)hz);
     if(p>s->max_fine_power){s->max_fine_power=p;s->best_hz=(uint32_t)hz;}
   }
   s->bin_index++;s->bins_measured++;return 0;
 }
 /* Parseval: for a single sinusoid, Goertzel power ~N*signal_energy/2.
  * Only lock a clearly dominant narrowband component; if it is broadband
  * noise the peak fraction should remain near zero. */
 const float denom=(float)TC_HUM_CAL_SAMPLES*s->total_power;
 float fraction=denom>0.0f?2.0f*s->max_fine_power/denom:0.0f;
 if(fraction>1.0f)fraction=1.0f;
 s->peak_fraction_permille=(uint32_t)(fraction*1000.0f+0.5f);
 if(s->peak_fraction_permille<300U || !s->best_hz) s->state=TC_HUM_FAILED;
 else s->state=TC_HUM_DONE;
 return 1;
}
