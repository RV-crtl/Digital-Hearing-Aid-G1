/* R26.4 host regression with synthetic S24 words; not a PCB measurement. */
#include "tc_s24_recovery.h"
#include <assert.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#define N 128U
#define PI 3.14159265358979323846
static uint32_t pack24(int32_t x) { return (uint32_t)x & UINT32_C(0x00ffffff); }
static void make_tone(uint32_t *data, double amplitude, unsigned hz, int opposite) {
    for (unsigned i=0;i<N;i++) {
        int32_t x=(int32_t)lrint(amplitude * sin(2.0 * PI * (double)hz * i / 48000.0));
        data[2*i] = pack24(x);
        data[2*i+1] = pack24(opposite ? -x : x);
    }
}
static uint32_t abs32(int x) {return (uint32_t)(x<0?-x:x);}
static void test_quiet_low_amplitude_1khz(void) {
    uint32_t w[2*N];int16_t l[N],r[N];TcS24Recovery s;
    TcS24Recovery_Init(&s);
    make_tone(w,1200,1000,0);
    for(int j=0;j<128;j++) assert(TcS24Recovery_Process(&s,w,N,l,r));
    assert(s.left_rms24 >= 700 && s.left_rms24 < 1000);
    assert(s.out_rms16 > 800 && s.out_rms16 < 2300);
    assert(s.gain_q16 > 65536 && s.gain_q16 < 4*65536);
    assert(s.clipped_samples==0);
    /* signed sine waveform has no gate and keeps both polarities */
    int pos=0,neg=0,zero=0;
    for(unsigned i=0;i<N;i++){pos+=(l[i]>0);neg+=(l[i]<0);zero+=(l[i]==0);assert(l[i]==r[i]);}
    assert(pos>25 && neg>25 && zero<15);
}
static void test_no_level_threshold_gate(void) {
    uint32_t w[2*N]={0}; int16_t l[N],r[N];TcS24Recovery s;
    TcS24Recovery_Init(&s);
    for(unsigned i=0;i<N;i++)w[2*i]=pack24(i&1?-7:7);
    for(int j=0;j<20;j++) assert(TcS24Recovery_Process(&s,w,N,l,r));
    assert(s.gain_q16==65536); /* no rising AGC on steady near-zero */
    for(unsigned i=0;i<N;i++) { assert(l[i]==(i&1?-7:7));assert(r[i]==0); }
}
static void test_full_scale_headroom(void) {
    uint32_t w[2*N];int16_t l[N],r[N];TcS24Recovery s;
    TcS24Recovery_Init(&s);
    make_tone(w,8000000,1000,0);
    assert(TcS24Recovery_Process(&s,w,N,l,r));
    assert(s.max_peak24 >= 7900000);
    assert(s.gain_q16 < 65536);
    assert(s.clipped_samples == 0);
    for(unsigned i=0;i<N;i++) { assert(abs32(l[i]) <= 27000); assert(abs32(r[i]) <= 27000); }
}
static void test_cap_and_phase(void) {
    uint32_t w[2*N];int16_t l[N],r[N];TcS24Recovery s;
    TcS24Recovery_Init(&s);
    TcS24Recovery_SetCap(&s,65536U);
    make_tone(w,200,1000,1);
    for(int j=0;j<80;j++) assert(TcS24Recovery_Process(&s,w,N,l,r));
    assert(s.gain_q16 == 65536U);
    assert(s.out_rms16>100 && s.out_rms16<180);
    for(unsigned i=0;i<N;i++)assert((int)l[i]+(int)r[i]==0);
    /* if switched to MIX, opposite channels can cancel; AUTO must select
     * one physical channel, not a corrupted PCM decode. */
}
static void test_sign_extension(void) {
    uint32_t w[2*N];int16_t l[N],r[N];TcS24Recovery s;
    TcS24Recovery_Init(&s);
    for(unsigned i=0;i<N;i++){w[2*i]=pack24(i&1?-800:800);w[2*i+1]=pack24(i&1?-800:800);}
    assert(TcS24Recovery_Process(&s,w,N,l,r));
    for(unsigned i=0;i<N;i++) {assert((i&1)?l[i]<-790:l[i]>790);assert(l[i]==r[i]);}
    assert(s.left_rms24==800);
    assert(s.out_rms16>=800 && s.out_rms16<1000);
}
int main(void){
    test_quiet_low_amplitude_1khz();
    test_no_level_threshold_gate();
    test_full_scale_headroom();
    test_cap_and_phase();
    test_sign_extension();
    puts("PASS R26.4: signed S24 recovery, 1kHz, low-level continuity, peak safety, phase, gain cap");
    puts("LIMITATION: synthetic PCM, not real PCM1808/I2S capture");
    return 0;
}
