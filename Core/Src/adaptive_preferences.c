#include "adaptive_preferences.h"
#include <limits.h>
#include <stddef.h>
static bool locate(G1PrefAmplitude amplitude, G1PrefSpectrum spectrum,
                   unsigned *bucket) {
    if (!bucket || (unsigned)amplitude>=G1_PREF_AMPLITUDE_BINS ||
        (unsigned)spectrum>=G1_PREF_SPECTRAL_BINS) return false;
    *bucket=(unsigned)amplitude*G1_PREF_SPECTRAL_BINS+(unsigned)spectrum;
    return true;
}
void G1Pref_Init(G1AdaptivePreferences *learner) {
    if (!learner) return;
    *learner=(G1AdaptivePreferences){0};
}
int16_t G1Pref_Get(const G1AdaptivePreferences *learner,
                   G1PrefAmplitude amplitude, G1PrefSpectrum spectrum) {
    unsigned index;
    if (!learner || !locate(amplitude,spectrum,&index)) return 0;
    return learner->gain_db_q8[index];
}
bool G1Pref_Observe(G1AdaptivePreferences *learner,
                    G1PrefAmplitude amplitude, G1PrefSpectrum spectrum) {
    unsigned index;
    if (!learner || !locate(amplitude,spectrum,&index)) return false;
    if (learner->observations[index]<UINT16_MAX) learner->observations[index]++;
    /* Observing louder environments is NOT consent to increase volume. */
    return true;
}
bool G1Pref_Feedback(G1AdaptivePreferences *learner,
                     G1PrefAmplitude amplitude, G1PrefSpectrum spectrum,
                     G1PrefAction action) {
    unsigned index;
    if (!learner || !locate(amplitude,spectrum,&index) ||
        (action!=G1_PREF_INCREASE && action!=G1_PREF_DECREASE)) return false;
    uint16_t events=learner->feedback_events[index];
    /* More observations do not accelerate learning. Repeated input has a
     * diminishing step to reduce unstable oscillation near user preference. */
    int32_t step=G1_PREF_STEP_DB_Q8/(1+(int32_t)(events/4U));
    if (step<8) step=8;
    int32_t next=(int32_t)learner->gain_db_q8[index]+
                  (action==G1_PREF_INCREASE?step:-step);
    if (next>G1_PREF_MAX_GAIN_DB_Q8) next=G1_PREF_MAX_GAIN_DB_Q8;
    if (next<G1_PREF_MIN_GAIN_DB_Q8) next=G1_PREF_MIN_GAIN_DB_Q8;
    learner->gain_db_q8[index]=(int16_t)next;
    if (events<UINT16_MAX) learner->feedback_events[index]++;
    return true;
}
