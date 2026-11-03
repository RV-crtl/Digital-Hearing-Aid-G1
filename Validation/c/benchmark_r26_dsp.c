/* Reproducible HOST throughput measurement, not an MCU-cycle or SPL claim.
 * Runs the exact production-C algorithms against deterministic input buffers.
 * No allocator, OS threads, HAL or serial prints are used in processing calls.
 * A pass indicates finite output/stable state, NOT a hard real-time guarantee. */
#include "g1_ambient_dsp.h"
#include "tc_cleanup.h"
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#define FRAMES 128U
#define BLOCKS 12000U
static uint32_t rng=0x31415926U;
static int16_t noise_sample(void){
  rng=rng*1664525U+1013904223U;
  return (int16_t)((int32_t)((rng>>16U)&0x1FFFU)-4096);
}
static volatile int32_t sink;
int main(void){
  static G1AmbientDsp ambient;
  static TcCleanup telecoil;
  static int16_t input[FRAMES],output[FRAMES];
  G1Ambient_Init(&ambient);
  TcCleanup_Reset(&telecoil);
  clock_t begin=clock();
  if(begin==(clock_t)-1)return 2;
  for(unsigned block=0;block<BLOCKS;block++){
    for(size_t i=0;i<FRAMES;i++){
      /* Deterministic wide-band noise plus 32-sample period tone. */
      int32_t x=noise_sample()/2+(((i+block)%32U)<16U?1800:-1800);
      input[i]=(int16_t)x;
    }
    G1Ambient_Process(&ambient,input,output,FRAMES);
    TcCleanup_Block(&telecoil,input,output,FRAMES);
    sink+=output[block%FRAMES]/64;
  }
  clock_t end=clock();
  if(end==(clock_t)-1||end<begin)return 3;
  const double elapsed=(double)(end-begin)/(double)CLOCKS_PER_SEC;
  const double audio=(double)BLOCKS*FRAMES/48000.0;
  if(ambient.blocks!=BLOCKS || elapsed<=0.0)return 4;
  printf("HOST_DSP_BENCH blocks=%u frames=%u cpu_seconds=%.6f audio_seconds=%.3f host_utilization_pct=%.3f\n",
    BLOCKS,FRAMES,elapsed,audio,100.0*elapsed/audio);
  printf("HOST_DSP_STATE ambient_gain_q12=%u trained_blocks=%lu tc_noise_floor=%lu limited_samples=%lu checksum=%ld\n",
    (unsigned)ambient.gain_q12,(unsigned long)ambient.learner.trained_blocks,
    (unsigned long)telecoil.noise_floor_rms,(unsigned long)telecoil.limiter_samples,(long)sink);
  puts("NOTE: not ARM CPU utilization; measure AudioTask runtime, ISR load and DMA underruns on target.");
  return 0;
}
