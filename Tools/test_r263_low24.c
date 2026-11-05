/* R26.3 portable decoder test: SAI samples simulated, not physical PCM1808 evidence. */
#include "pcm_transport.h"
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <math.h>
#define N 128U
#define PI 3.14159265358979323846
static uint32_t pack_s24(int16_t sample) {
  /* 24-bit signed sample with eight low bits truncated to zero */
  const int32_t x = (int32_t)sample * 256;
  return ((uint32_t)x) & UINT32_C(0x00FFFFFF);
}
static void test_boundaries(void) {
  assert(PcmTransport_DecodeWordMode(0x007FFFFFU,PCM_DECODE_LOW24) == 32767);
  assert(PcmTransport_DecodeWordMode(0x00800000U,PCM_DECODE_LOW24) == -32768);
  assert(PcmTransport_DecodeWordMode(0x00FF0000U,PCM_DECODE_LOW24) == -256);
  assert(PcmTransport_DecodeWordMode(0x000000FFU,PCM_DECODE_LOW24) == 0);
  assert(PcmTransport_DecodeWordMode(0x007F0000U,PCM_DECODE_LOW16) == 0);
  assert(PcmTransport_DecodeWordMode(0x0000FF80U,PCM_DECODE_LOW16) == -128);
}
static void test_realistic_tone(void) {
  uint32_t words[2*N];int16_t left[N],right[N];
  for(unsigned i=0;i<N;++i) {
    int16_t s=(int16_t)lrint(1500.0*sin(2.0*PI*1000.0*(double)i/48000.0));
    words[2*i]=pack_s24(s);
    words[2*i+1]=pack_s24((int16_t)(s/-2));
  }
  assert(PcmTransport_DecodeStereoWordsMode(words,N,left,right,PCM_DECODE_LOW24)==PCM_TRANSPORT_OK);
  for(unsigned i=0;i<N;++i) {
    int16_t expected=(int16_t)lrint(1500.0*sin(2.0*PI*1000.0*(double)i/48000.0));
    assert(left[i]==expected);assert(right[i]==(int16_t)(expected/-2));
  }
  double e=0.0;
  for(unsigned i=0;i<N;++i)e+=(double)left[i]*left[i];
  assert(sqrt(e/N)>900.0);
  printf("PASS R26.3 LOW24: signed S24 -> S16 tone 1kHz, stereo channels and 24-bit limits\n");
}
int main(void){test_boundaries();test_realistic_tone();puts("R26.3 portable PCM PASS (NOT hardware evidence)");return 0;}
