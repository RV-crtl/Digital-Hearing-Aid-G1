#ifndef G1_SCENE_FEATURES_H
#define G1_SCENE_FEATURES_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "adaptive_preferences.h"
#ifdef __cplusplus
extern "C" {
#endif
/*
 * Low-overhead PCM scene context for experimental preference learning.
 * Deterministic, no dynamic allocation and no peripheral dependencies.
 * Computes three exact DFT bins at 48 kHz / 256 samples via Goertzel.
 * NOT an FFT, sound-level meter, clinical classifier or field-trained AI.
 * Gain caps and output protection remain separate requirements.
 */
#define G1_SCENE_SAMPLES 256U
#define G1_SCENE_SAMPLE_RATE 48000U
typedef struct {
    uint32_t rms_pcm;
    uint32_t low_energy;
    uint32_t mid_energy;
    uint32_t high_energy;
    G1PrefAmplitude amplitude;
    G1PrefSpectrum spectrum;
} G1SceneFeatures;
/* Input exactly 256 signed PCM16 mono samples at 48 kHz.
 * DFT bins: 187.5 Hz, 937.5 Hz, 3937.5 Hz; representative features,
 * not full-band integrated power. */
bool G1Scene_Extract(const int16_t *pcm, size_t samples, G1SceneFeatures *out);
#ifdef __cplusplus
}
#endif
#endif
