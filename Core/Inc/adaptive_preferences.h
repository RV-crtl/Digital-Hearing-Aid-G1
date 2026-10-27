#ifndef G1_ADAPTIVE_PREFERENCES_H
#define G1_ADAPTIVE_PREFERENCES_H
#include <stdbool.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
/*
 * Experimental bounded contextual preference learner. No dynamic allocation,
 * no autonomous high-gain excursions and no peripheral/storage dependencies.
 * NOT connected to a custom PCB in the present operating firmware.
 *
 * Context: coarse ambient amplitude (quiet/moderate/loud) and spectral
 * centroid bucket (low/mid/high). Each bucket has one learned gain offset.
 * Units are 1/256 dB (Q8). Offsets constrained to [-6,+3] dB.
 * Explicit user feedback is the only learning event; observation alone
 * must never change gain.
 */
#define G1_PREF_AMPLITUDE_BINS 3U
#define G1_PREF_SPECTRAL_BINS 3U
#define G1_PREF_BUCKET_COUNT 9U
#define G1_PREF_MIN_GAIN_DB_Q8 (-1536)
#define G1_PREF_MAX_GAIN_DB_Q8 (768)
#define G1_PREF_STEP_DB_Q8 192
typedef enum {
    G1_PREF_QUIET=0, G1_PREF_MODERATE=1, G1_PREF_LOUD=2
} G1PrefAmplitude;
typedef enum {
    G1_PREF_LOW=0, G1_PREF_MID=1, G1_PREF_HIGH=2
} G1PrefSpectrum;
typedef enum {
    G1_PREF_DECREASE=-1, G1_PREF_INCREASE=1
} G1PrefAction;
typedef struct {
    int16_t gain_db_q8[G1_PREF_BUCKET_COUNT];
    uint16_t observations[G1_PREF_BUCKET_COUNT];
    uint16_t feedback_events[G1_PREF_BUCKET_COUNT];
} G1AdaptivePreferences;
void G1Pref_Init(G1AdaptivePreferences *learner);
/* Classification thresholds are application-owned; this learner accepts
 * validated coarse context only. */
int16_t G1Pref_Get(const G1AdaptivePreferences *learner,
                   G1PrefAmplitude amplitude, G1PrefSpectrum spectrum);
bool G1Pref_Observe(G1AdaptivePreferences *learner,
                    G1PrefAmplitude amplitude, G1PrefSpectrum spectrum);
bool G1Pref_Feedback(G1AdaptivePreferences *learner,
                     G1PrefAmplitude amplitude, G1PrefSpectrum spectrum,
                     G1PrefAction action);
#ifdef __cplusplus
}
#endif
#endif
