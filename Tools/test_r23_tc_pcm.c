/* Host-only deterministic PCM/detector regression. Never synthetic hardware evidence. */
#include "pcm_transport.h"
#include "telecoil_detector.h"
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <math.h>
#define FRAMES 128U
int main(void) {
  uint32_t words[FRAMES*2U]; int16_t l[FRAMES],r[FRAMES];
  /* 800-RMS 2-kHz test modeled as user-style LOW16 words: upper bits do NOT
   * encode S24 PCM in the same alignment. This tests code interpretation, not ADC. */
  for(unsigned i=0;i<FRAMES;i++){
    double val=1100.0*sin(2.0*3.14159265358979323846*2000.0*(double)i/48000.0);
    int16_t pcm=(int16_t)lround(val);
    words[2*i]=0x00000000U | (uint16_t)pcm;
    words[2*i+1]=0x00000000U | (uint16_t)pcm;
  }
  if(PcmTransport_DecodeStereoWordsMode(words,FRAMES,l,r,PCM_DECODE_LOW16)!=PCM_TRANSPORT_OK)return 1;
  if(memcmp(l,r,sizeof l)!=0)return 2;
  TelecoilDetectorConfig_t cfg=TelecoilDetector_DefaultConfig();
  if(cfg.present_rms_threshold!=550U || cfg.absent_rms_threshold!=300U || cfg.min_peak_to_peak!=800U)return 3;
  TelecoilDetector_t det;
  if(TelecoilDetector_Init(&det,&cfg)!=TELECOIL_DETECTOR_OK)return 4;
  TelecoilBlockMetrics_t metric={0};
  for(int k=0;k<30;k++){
    if(TelecoilDetector_Process(&det,l,FRAMES,&metric)!=TELECOIL_DETECTOR_OK)return 5;
  }
  if(!metric.valid_signal || metric.rms<700U || metric.rms>900U || metric.peak_to_peak<2100U)return 6;
  memset(l,0,sizeof l);
  for(int k=0;k<90;k++){
    if(TelecoilDetector_Process(&det,l,FRAMES,&metric)!=TELECOIL_DETECTOR_OK)return 7;
  }
  if(metric.valid_signal || metric.rms!=0)return 8;
  if(PcmTransport_DecodeWordMode(0x00FFFF81U,PCM_DECODE_LOW16)!=(int16_t)-127)return 9;
  if(PcmTransport_DecodeWordMode(0x00000635U,PCM_DECODE_LOW16)!=(int16_t)0x0635)return 10;
  printf("PASS R23 host PCM: LOW16 signed input, calibrated detector hysteresis, silent rejection (not hardware)\n");
  return 0;
}
