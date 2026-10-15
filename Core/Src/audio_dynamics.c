/**
 * @file audio_dynamics.c
 * @brief Speech-preserving DSP tuned for the final bone-conduction output.
 */
#include "audio_dynamics.h"

#include <limits.h>

/*
 * Coefficients are RBJ-style biquads calculated for 48 kHz.
 *
 * HPF: 2nd-order Butterworth, fc = 150 Hz.
 * This removes DC/sub-bass that wastes bone-conductor headroom while leaving
 * 300 Hz and above essentially intact.
 */
#define HP_B0  (0.9862119246f)
#define HP_B1  (-1.9724238493f)
#define HP_B2  (0.9862119246f)
#define HP_A1  (-1.9722337292f)
#define HP_A2  (0.9726139693f)

/*
 * Broad resonance compensation: fc = 1.6 kHz, Q = 1.2, gain = -3 dB.
 * The selected bone conductor is specified with a resonance around 1.6 kHz.
 * This is intentionally gentle; it is independently switchable at runtime.
 */
#define EQ_B0  (0.9727371367f)
#define EQ_B1  (-1.7736777085f)
#define EQ_B2  (0.8405655869f)
#define EQ_A1  (-1.7736777085f)
#define EQ_A2  (0.8133027236f)

static bool config_is_valid(const AudioDynamicsConfig_t *c)
{
    return (c != NULL) &&
           (c->sample_rate_hz >= 16000U) &&
           (c->compressor_threshold_mean_abs > 0U) &&
           (c->compressor_ratio_x100 >= 100U) &&
           (c->makeup_gain_q15 > 0) &&
           (c->min_gain_q15 > 0) &&
           (c->max_gain_q15 >= c->min_gain_q15) &&
           (c->gain_attack_alpha_q15 <= 32768U) &&
           (c->gain_release_alpha_q15 <= 32768U) &&
           (c->peak_target > 0) && (c->peak_target <= INT16_MAX) &&
           (c->soft_limit_start > 0) &&
           (c->soft_limit_max > c->soft_limit_start) &&
           (c->soft_limit_max <= INT16_MAX);
}

static float f_abs(float x)
{
    return (x < 0.0f) ? -x : x;
}

static float f_clamp(float x, float lo, float hi)
{
    if (x < lo) return lo;
    if (x > hi) return hi;
    return x;
}

static int16_t float_to_s16(float x)
{
    if (x > 32767.0f) return 32767;
    if (x < -32768.0f) return -32768;
    return (int16_t)((x >= 0.0f) ? (x + 0.5f) : (x - 0.5f));
}

static float biquad_process(float x,
                            float b0, float b1, float b2,
                            float a1, float a2,
                            float *x1, float *x2,
                            float *y1, float *y2)
{
    const float y = (b0 * x) + (b1 * (*x1)) + (b2 * (*x2))
                  - (a1 * (*y1)) - (a2 * (*y2));
    *x2 = *x1;
    *x1 = x;
    *y2 = *y1;
    *y1 = y;
    return y;
}

static void reset_filter_state(AudioDynamics_t *s)
{
    s->hp_x1 = s->hp_x2 = s->hp_y1 = s->hp_y2 = 0.0f;
    s->eq_x1 = s->eq_x2 = s->eq_y1 = s->eq_y2 = 0.0f;
}

static float soft_limit(AudioDynamics_t *s, float x)
{
    const float start = (float)s->config.soft_limit_start;
    const float maximum = (float)s->config.soft_limit_max;
    const float range = maximum - start;
    const float ax = f_abs(x);

    if (ax <= start) return x;

    ++s->last_limiter_samples;
    ++s->total_limiter_samples;

    const float over = ax - start;
    float y = start + ((over * range) / (over + range));
    if (y > maximum) y = maximum;
    return (x < 0.0f) ? -y : y;
}

AudioDynamicsConfig_t AudioDynamics_DefaultConfig(void)
{
    AudioDynamicsConfig_t c;
    c.sample_rate_hz = 48000U;

    /* Gentle compression only. Quiet speech is not normalised upward. */
    c.compressor_threshold_mean_abs = 9000U;
    c.compressor_ratio_x100 = 180U;     /* 1.8:1 above threshold. */
    c.makeup_gain_q15 = 32768;          /* Unity makeup; output level is controlled separately. */
    c.min_gain_q15 = 14746;             /* 0.45x. */
    c.max_gain_q15 = 34406;             /* 1.05x. */

    /* 128 frames at 48 kHz = 2.667 ms. ~40 ms attack, ~300 ms release. */
    c.gain_attack_alpha_q15 = 30655U;
    c.gain_release_alpha_q15 = 32478U;

    c.peak_target = 30500;
    c.soft_limit_start = 30000;
    c.soft_limit_max = 32600;
    c.resonance_eq_default = true;
    c.compressor_default = true;
    return c;
}

AudioDynamicsResult_t AudioDynamics_Init(AudioDynamics_t *state,
                                         const AudioDynamicsConfig_t *config)
{
    if (state == NULL) return AUDIO_DYNAMICS_ERR_ARGUMENT;
    if (!config_is_valid(config)) return AUDIO_DYNAMICS_ERR_CONFIG;

    *state = (AudioDynamics_t){0};
    state->config = *config;
    state->current_gain = 1.0f;
    state->current_gain_q15 = 32768;
    state->resonance_eq_enabled = config->resonance_eq_default;
    state->compressor_enabled = config->compressor_default;
    state->gate_open = true;
    state->initialised = true;
    return AUDIO_DYNAMICS_OK;
}

AudioDynamicsResult_t AudioDynamics_Reset(AudioDynamics_t *state)
{
    if (state == NULL) return AUDIO_DYNAMICS_ERR_ARGUMENT;
    if (!state->initialised) return AUDIO_DYNAMICS_ERR_STATE;

    reset_filter_state(state);
    state->current_gain = 1.0f;
    state->current_gain_q15 = 32768;
    state->gate_open = true;
    state->last_mean_abs = 0U;
    state->last_peak = 0U;
    state->last_limiter_samples = 0U;
    return AUDIO_DYNAMICS_OK;
}

void AudioDynamics_SetResonanceEq(AudioDynamics_t *state, bool enabled)
{
    if ((state == NULL) || !state->initialised) return;
    state->resonance_eq_enabled = enabled;
    /* Clear only the EQ history to avoid a stale-state thump when toggled. */
    state->eq_x1 = state->eq_x2 = state->eq_y1 = state->eq_y2 = 0.0f;
}

void AudioDynamics_SetCompression(AudioDynamics_t *state, bool enabled)
{
    if ((state == NULL) || !state->initialised) return;
    state->compressor_enabled = enabled;
    state->current_gain = 1.0f;
    state->current_gain_q15 = 32768;
}

int16_t AudioDynamics_LimitS16(int32_t sample, int32_t limit)
{
    if (limit <= 0) return 0;
    if (limit > INT16_MAX) limit = INT16_MAX;
    if (sample > limit) return (int16_t)limit;
    if (sample < -limit) return (int16_t)-limit;
    return (int16_t)sample;
}

int32_t AudioDynamics_TargetGainQ15(const AudioDynamicsConfig_t *config,
                                    uint32_t peak)
{
    if (!config_is_valid(config)) return 0;
    if (peak == 0U) return config->max_gain_q15;

    int64_t g = ((int64_t)config->peak_target << 15) / (int64_t)peak;
    if (g < config->min_gain_q15) g = config->min_gain_q15;
    if (g > config->max_gain_q15) g = config->max_gain_q15;
    return (int32_t)g;
}

AudioDynamicsResult_t AudioDynamics_ProcessBlock(AudioDynamics_t *state,
                                                 const int16_t *input,
                                                 int16_t *output,
                                                 size_t sample_count,
                                                 bool muted)
{
    if (state == NULL) return AUDIO_DYNAMICS_ERR_ARGUMENT;
    if (!state->initialised) return AUDIO_DYNAMICS_ERR_STATE;
    if (sample_count == 0U) return AUDIO_DYNAMICS_OK;
    if ((input == NULL) || (output == NULL)) return AUDIO_DYNAMICS_ERR_ARGUMENT;

    state->last_limiter_samples = 0U;

    if (muted) {
        for (size_t i = 0U; i < sample_count; ++i) output[i] = 0;
        reset_filter_state(state);
        state->current_gain = 1.0f;
        state->current_gain_q15 = 32768;
        state->gate_open = false;
        state->last_mean_abs = 0U;
        state->last_peak = 0U;
        return AUDIO_DYNAMICS_OK;
    }

    state->gate_open = true;

    /*
     * Pass 1: linear, time-invariant filtering only.  Unlike the previous DSP,
     * no gain detector feeds back sample-by-sample into the speech waveform.
     */
    float sum_abs = 0.0f;
    float peak = 0.0f;

    for (size_t i = 0U; i < sample_count; ++i) {
        float y = biquad_process((float)input[i],
                                 HP_B0, HP_B1, HP_B2, HP_A1, HP_A2,
                                 &state->hp_x1, &state->hp_x2,
                                 &state->hp_y1, &state->hp_y2);

        if (state->resonance_eq_enabled) {
            y = biquad_process(y,
                               EQ_B0, EQ_B1, EQ_B2, EQ_A1, EQ_A2,
                               &state->eq_x1, &state->eq_x2,
                               &state->eq_y1, &state->eq_y2);
        }

        output[i] = float_to_s16(y);
        const float a = f_abs(y);
        sum_abs += a;
        if (a > peak) peak = a;
    }

    const float mean_abs = sum_abs / (float)sample_count;
    state->last_mean_abs = (uint32_t)(mean_abs + 0.5f);
    state->last_peak = (uint32_t)(peak + 0.5f);

    /*
     * Slow broadband compressor.  The old block AGC tried to hit a loudness
     * target every 2.67 ms, which can turn low-frequency voiced content into
     * audible amplitude modulation.  Here, quiet material stays at unity; only
     * louder material is gently compressed and the control gain moves slowly.
     */
    float desired_gain = 1.0f;

    if (state->compressor_enabled &&
        mean_abs > (float)state->config.compressor_threshold_mean_abs) {
        const float threshold = (float)state->config.compressor_threshold_mean_abs;
        const float ratio = (float)state->config.compressor_ratio_x100 / 100.0f;
        const float compressed_level = threshold + ((mean_abs - threshold) / ratio);
        desired_gain = compressed_level / mean_abs;
    }

    desired_gain *= (float)state->config.makeup_gain_q15 / 32768.0f;
    desired_gain = f_clamp(desired_gain,
                           (float)state->config.min_gain_q15 / 32768.0f,
                           (float)state->config.max_gain_q15 / 32768.0f);

    /* Peak safety influences the slow gain control but does not hard-step it. */
    if (peak > 1.0f) {
        const float peak_safe = (float)state->config.peak_target / peak;
        if (peak_safe < desired_gain) desired_gain = peak_safe;
    }

    const uint16_t alpha_q15 = (desired_gain < state->current_gain)
        ? state->config.gain_attack_alpha_q15
        : state->config.gain_release_alpha_q15;
    const float alpha = (float)alpha_q15 / 32768.0f;
    const float gain_start = state->current_gain;
    float gain_end = (alpha * gain_start) + ((1.0f - alpha) * desired_gain);
    gain_end = f_clamp(gain_end,
                       (float)state->config.min_gain_q15 / 32768.0f,
                       (float)state->config.max_gain_q15 / 32768.0f);

    for (size_t i = 0U; i < sample_count; ++i) {
        float g = gain_end;
        if (sample_count > 1U) {
            const float t = (float)i / (float)(sample_count - 1U);
            g = gain_start + ((gain_end - gain_start) * t);
        }
        const float y = soft_limit(state, (float)output[i] * g);
        output[i] = float_to_s16(y);
    }

    state->current_gain = gain_end;
    state->current_gain_q15 = (int32_t)(gain_end * 32768.0f + 0.5f);
    return AUDIO_DYNAMICS_OK;
}
