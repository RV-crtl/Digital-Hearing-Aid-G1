/**
 * @file g1_ambient_dsp.h
 * @brief Wide-environment listening conditioning and bounded feedback AGC.
 *
 * Intended to preserve speech, traffic, footsteps and general surrounding
 * sounds. No hard silence gate or inference of safety-critical events.
 * Operates on 128-frame 48-kSa/s mono PCM blocks from the DFSDM microphone.
 * AudioTask is sole owner. All allocations are static; no DMA/ISR processing.
 * Output is limited digitally; electroacoustic SPL is NOT calibrated.
 */
#ifndef G1_AMBIENT_DSP_H
#define G1_AMBIENT_DSP_H
#include <stddef.h>
#include <stdint.h>
#include "g1_scene_ml.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct {
  G1SceneLearner learner;
  int32_t dc_lp_q8;
  uint32_t gain_q12;
  uint32_t input_rms, input_peak;
  uint32_t ambient_floor_rms;
  uint32_t output_rms, limited_samples, blocks;
  uint16_t zero_crossings;
  uint8_t scene_cluster, amplitude_bin, spectrum_bin;
} G1AmbientDsp;
void G1Ambient_Init(G1AmbientDsp *st);
void G1Ambient_Process(G1AmbientDsp *st,const int16_t *in,int16_t *out,size_t n);
#ifdef __cplusplus
}
#endif
#endif
