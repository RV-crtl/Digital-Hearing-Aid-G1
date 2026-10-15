/**
 * @file audio_dynamics.h
 * @brief Natural-sounding speech/bone-conduction DSP for STM32L476.
 *
 * The previous block-normalising AGC could audibly modulate low-frequency
 * voiced speech.  This revision uses only linear filtering in the speech band,
 * a slow gentle compressor, and a near-full-scale soft limiter.
 *
 * Signal path:
 *   2nd-order 150 Hz HPF -> optional broad 1.6 kHz resonance compensation ->
 *   slow broadband compression -> soft limiter.
 *
 * There is deliberately no noise gate and no fast loudness normaliser in the
 * default path.  This preserves pitch, formants and natural speech dynamics.
 */
#ifndef AUDIO_DYNAMICS_H
#define AUDIO_DYNAMICS_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    AUDIO_DYNAMICS_OK = 0,
    AUDIO_DYNAMICS_ERR_ARGUMENT,
    AUDIO_DYNAMICS_ERR_CONFIG,
    AUDIO_DYNAMICS_ERR_STATE
} AudioDynamicsResult_t;

typedef struct {
    uint32_t sample_rate_hz;
    uint32_t compressor_threshold_mean_abs;
    uint16_t compressor_ratio_x100;     /**< 180 = 1.80:1. */
    int32_t makeup_gain_q15;            /**< Nominal gain after compression. */
    int32_t min_gain_q15;
    int32_t max_gain_q15;
    uint16_t gain_attack_alpha_q15;     /**< Per-block smoothing alpha when reducing gain. */
    uint16_t gain_release_alpha_q15;    /**< Per-block smoothing alpha when restoring gain. */
    int32_t peak_target;                /**< Slow gain safety target. */
    int32_t soft_limit_start;
    int32_t soft_limit_max;
    bool resonance_eq_default;
    bool compressor_default;
} AudioDynamicsConfig_t;

typedef struct {
    AudioDynamicsConfig_t config;

    /* Floating point biquad states. STM32L476 has a Cortex-M4F FPU. */
    float hp_x1, hp_x2, hp_y1, hp_y2;
    float eq_x1, eq_x2, eq_y1, eq_y2;
    float current_gain;

    int32_t current_gain_q15;
    bool resonance_eq_enabled;
    bool compressor_enabled;
    bool gate_open;                     /**< Kept for existing telemetry; always true when unmuted. */
    bool initialised;

    uint32_t last_mean_abs;
    uint32_t last_peak;
    uint32_t last_limiter_samples;
    uint32_t total_limiter_samples;
} AudioDynamics_t;

AudioDynamicsConfig_t AudioDynamics_DefaultConfig(void);

AudioDynamicsResult_t AudioDynamics_Init(AudioDynamics_t *state,
                                         const AudioDynamicsConfig_t *config);
AudioDynamicsResult_t AudioDynamics_Reset(AudioDynamics_t *state);

void AudioDynamics_SetResonanceEq(AudioDynamics_t *state, bool enabled);
void AudioDynamics_SetCompression(AudioDynamics_t *state, bool enabled);

int16_t AudioDynamics_LimitS16(int32_t sample, int32_t limit);
int32_t AudioDynamics_TargetGainQ15(const AudioDynamicsConfig_t *config,
                                    uint32_t peak);

AudioDynamicsResult_t AudioDynamics_ProcessBlock(AudioDynamics_t *state,
                                                 const int16_t *input,
                                                 int16_t *output,
                                                 size_t sample_count,
                                                 bool muted);

#ifdef __cplusplus
}
#endif

#endif /* AUDIO_DYNAMICS_H */
