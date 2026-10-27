#ifndef G1_PREFERENCE_LEARNER_H
#define G1_PREFERENCE_LEARNER_H
#include <stdint.h>
#include <stddef.h>
/*
 * Bounded on-device contextual preference learner.
 * No malloc, no inference inside DMA ISR, no boost above unity, no EEPROM writes.
 * AudioTask/single owner. UI/encoder events must arrive through a bounded queue.
 * This is a portable, TESTED LIBRARY; no PCB user-input event integration is claimed.
 */
#define G1_PREF_LEVELS 3U
#define G1_PREF_BANDS 3U
#define G1_PREF_SOURCES 3U
#define G1_PREF_CONTEXTS (G1_PREF_LEVELS * G1_PREF_BANDS * G1_PREF_SOURCES)
#define G1_PREF_UNITY_Q15 32768U
typedef enum { G1_PREF_DOWN=-1, G1_PREF_NEUTRAL=0, G1_PREF_UP=1 } G1PrefAction;
typedef struct {
    int8_t preference; /* learned attenuation inclination -8..+8 */
    uint8_t feedback; /* bounded support count, 0..32 */
    uint32_t last_tick; /* last event: prevents feedback floods */
} G1PrefCell;
typedef struct {
    G1PrefCell cells[G1_PREF_CONTEXTS];
    uint32_t accepted, ignored;
    uint8_t enabled;
} G1PreferenceLearner;
void G1Pref_Init(G1PreferenceLearner *model);
void G1Pref_Enable(G1PreferenceLearner *model, uint8_t enabled);
void G1Pref_Reset(G1PreferenceLearner *model);
/* Band estimate uses bounded adjacent-sample difference / absolute magnitude;
 * spectral information is approximate, not an FFT or trained classifier. */
uint8_t G1Pref_BandFromPCM(const int16_t *pcm, size_t n);
uint8_t G1Pref_LevelFromMean(uint32_t mean_abs);
/* Return 1 only if accepted; reject invalid contexts, flood and disabled state.
 * tick is an arbitrary monotonic millisecond counter, wrap-safe. */
uint8_t G1Pref_Feedback(G1PreferenceLearner *model, uint8_t source,
                        uint8_t level, uint8_t band, G1PrefAction action,
                        uint32_t tick);
/* Recommended ATTENUATION only. Never produces gain > 32768 (unity).
 * Confidence is number of feedback actions supporting this context. */
uint32_t G1Pref_RecommendQ15(const G1PreferenceLearner *model, uint8_t source,
                              uint8_t level, uint8_t band, uint8_t *confidence);
#endif
