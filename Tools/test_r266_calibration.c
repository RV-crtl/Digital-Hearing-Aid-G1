/* R26.6 real C code, deterministic synthetic PCM tests: hardware not simulated. */
#include "tc_hum_cal.h"
#include "tc_cleanup.h"
#include "tc_s24_recovery.h"
#include <assert.h>
#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>
#include <math.h>
#include <string.h>
#define PI 3.14159265358979323846
#define FS 48000
#define N 128
#define T 48000
static uint32_t words[2*N];
static int16_t left[N],right[N],out[N];
static void render(TcS24Recovery *rec,TcCleanup *filter,
    int start,int count,unsigned humhz,double humamp,unsigned voicehz,double voiceamp,
    double *in,double *outf){
 for(int t=start;t<start+count;t+=N){
   for(int i=0;i<N;i++){
     int x=(int)lrint(humamp*sin(2.0*PI*(double)(t+i)*(double)humhz/FS)+
                      voiceamp*sin(2.0*PI*(double)(t+i)*(double)voicehz/FS));
     words[2*i]=(uint32_t)x & 0x00ffffffu;
     words[2*i+1]=(uint32_t)x & 0x00ffffffu;
   }
   assert(TcS24Recovery_Process(rec,words,N,left,right));
   TcCleanup_Block(filter,left,out,N);
   for(int i=0;i<N;i++){
      in[t-start+i]=left[i];outf[t-start+i]=out[i];
      assert(out[i]>=-28000 && out[i]<=28000);
   }
   assert(filter->gate_gain==1.0f && filter->signal_gain==1.0f);
 }
}
static double ampl(const double *a,size_t length,unsigned hz){
 double c=0,s=0;
 for(size_t i=0;i<length;i++){
  double phi=2.0*PI*(double)hz*(double)i/(double)FS;
  c+=a[i]*cos(phi);s+=a[i]*sin(phi);
 }
 return hypot(c,s)*2.0/(double)length;
}
static double before[T],after[T];
static unsigned learn(unsigned humhz,double humamp,unsigned extra_hz,double extra_amp){
 TcHumCal cal;TcS24Recovery rec;
 TcHumCal_Init(&cal);TcHumCal_Request(&cal);TcS24Recovery_Init(&rec);
 for(int t=0;t<18000;t+=N){
   for(int i=0;i<N;i++){
     int x=(int)lrint(humamp*sin(2.0*PI*(double)(t+i)*humhz/FS)+
                      extra_amp*sin(2.0*PI*(double)(t+i)*extra_hz/FS));
     words[2*i]=words[2*i+1]=((uint32_t)x & 0xffffffu);
   }
   assert(TcS24Recovery_Process(&rec,words,N,left,right));
   TcHumCal_Feed(&cal,left,N);
 }
 assert(cal.state==TC_HUM_ANALYSING);
 for(unsigned i=0;i<1000 && cal.state==TC_HUM_ANALYSING;i++)TcHumCal_Step(&cal);
 if(cal.state==TC_HUM_DONE){
   printf("cal locked %u Hz for injected %u Hz, fraction %.3f\n",cal.best_hz,humhz,
          cal.peak_fraction_permille/1000.0);
   assert(abs((int)cal.best_hz-(int)humhz)<=10);return cal.best_hz;
 }
 printf("cal rejected injection %u Hz, fraction=%.3f\n",humhz,
          cal.peak_fraction_permille/1000.0);
 return 0;
}
static void test_narrow_stationary_hum(unsigned humhz,unsigned wantedhz){
 unsigned learned=learn(humhz,3300,0,0);
 assert(learned);
 TcS24Recovery rec;TcCleanup clean;
 TcS24Recovery_Init(&rec);TcCleanup_Reset(&clean);
 /* Without calibrating, the default is a continuous, fixed bandpass. */
 assert(clean.notch_hz==0 && !clean.notch_auto_enabled);
 TcCleanup_SetNotchFrequency(&clean,learned);
 render(&rec,&clean,0,T,humhz,3300,wantedhz,800,before,after);
 double hb=ampl(before,T,humhz),ha=ampl(after,T,humhz);
 double wb=ampl(before,T,wantedhz),wa=ampl(after,T,wantedhz);
 printf("hum %u: before %.1f after %.1f (%.1f dB), %u Hz audio %.1f -> %.1f (%.1f%%)\n",
 humhz,hb,ha,20.0*log10(ha/hb),wantedhz,wb,wa,wa/wb*100.0);
 assert(ha/hb<0.28); /* >11 dB tonal suppression */
 assert(wa/wb>0.85); /* do not take the signal away */
 assert(clean.gate_gain==1.0f && clean.signal_gain==1.0f);
 /* Audio never drops below half its expected RMS in 100ms windows. */
 for(int offset=T/2;offset<T;offset+=4800){
  double accum=0;
  for(int i=offset;i<offset+4800;i++)accum+=after[i]*after[i];
  assert(sqrt(accum/4800.0)>200.0);
 }
 TcCleanup_SetNotchAuto(&clean,0);
 render(&rec,&clean,0,T,humhz,3300,wantedhz,800,before,after);
 assert(ampl(after,T,humhz)/ampl(before,T,humhz)>0.70);
}
static void test_no_auto_chasing(void){
 TcCleanup c;TcS24Recovery r;
 TcCleanup_Reset(&c);TcS24Recovery_Init(&r);
 render(&r,&c,0,T,240,3300,1000,800,before,after);
 assert(c.notch_hz==0U && c.gate_gain==1.0f);
 assert(ampl(after,T,1000)/ampl(before,T,1000)>0.9);
 assert(ampl(after,T,240)/ampl(before,T,240)>0.8);
}
int main(void){
 test_no_auto_chasing();
 test_narrow_stationary_hum(240,1000);
 test_narrow_stationary_hum(920,1500);
 test_narrow_stationary_hum(1840,1000);
 puts("PASS R26.6: source-off learned stationary frequencies, no auto chasing, continuous wanted signal and bypass");
 puts("LIMITATION: synthetic input only; board noise frequency, PCM alignment, SAI overrun, and acoustic SPL not measured");
 return 0;
}
