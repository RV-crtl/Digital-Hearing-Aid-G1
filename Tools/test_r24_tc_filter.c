/* Portable signal-response check only; NOT a physical noise or acoustic test. */
#include "tc_cleanup.h"
#include <stdint.h>
#include <math.h>
#include <stdio.h>
#define N 128U
#define PI 3.14159265358979323846
static double run_tone(double f) {
    TcCleanup s; TcCleanup_Reset(&s);
    int16_t in[N],out[N]; double acc=0.;
    for(unsigned block=0;block<130;++block){
      for(unsigned i=0;i<N;++i){
        const double t=(block*N+i)/48000.0;
        in[i]=(int16_t)lrint(1300.0*sin(2.0*PI*f*t));
      }
      TcCleanup_Block(&s,in,out,N);
      if(block>20)for(unsigned i=0;i<N;++i)acc+=(double)out[i]*out[i];
    }
    return sqrt(acc/((130-21)*N));
}
int main(void){
 double desired=run_tone(2000.0), hiss=run_tone(9000.0);
 if(desired<650. || desired>4500. || hiss>=desired*.40) {
   printf("FAIL TC DSP: 2kHz=%.1f 9kHz=%.1f\n",desired,hiss);return 1;
 }
 printf("PASS TC DSP R26.2: 2kHz RMS=%.1f, out-of-band 9kHz RMS=%.1f (simulation only)\n",desired,hiss);
 return 0;
}
