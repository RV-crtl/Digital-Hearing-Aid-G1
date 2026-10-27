#include "preference_learner.h"
#include <string.h>
#define PREF_MIN_FEEDBACK_INTERVAL_MS 250U
#define PREF_MIN_EVIDENCE 2U
#define PREF_MAX_COUNT 32U
#define PREF_MAX_BIAS 8
#define PREF_MIN_Q15 24576U /* never boost, at most -2.5 dB relative attenuation */

static int idx(uint8_t source,uint8_t level,uint8_t band) {
    if (source>=G1_PREF_SOURCES||level>=G1_PREF_LEVELS||band>=G1_PREF_BANDS)
        return -1;
    return (int)(source*9U+level*3U+band);
}
void G1Pref_Init(G1PreferenceLearner *m) {
    if (!m) return;
    memset(m,0,sizeof(*m));
}
void G1Pref_Enable(G1PreferenceLearner *m,uint8_t enabled) {
    if (m) m->enabled=(uint8_t)(enabled!=0U);
}
void G1Pref_Reset(G1PreferenceLearner *m) {
    if (!m) return;
    uint8_t enabled=m->enabled;
    memset(m,0,sizeof(*m));
    m->enabled=enabled;
}
uint8_t G1Pref_LevelFromMean(uint32_t mean_abs) {
    return mean_abs<240U?0U:(mean_abs>7500U?2U:1U);
}
uint8_t G1Pref_BandFromPCM(const int16_t *pcm,size_t n) {
    if (!pcm || n<2U) return 1U;
    uint64_t diff=0,mag=0;
    int32_t prev=pcm[0];
    for (size_t i=1;i<n;i++) {
        int32_t now=pcm[i],delta=now-prev;
        diff+=(uint32_t)(delta<0?-delta:delta);
        mag+=(uint32_t)(now<0?-now:now);
        prev=now;
    }
    if (mag<16U) return 1U;
    /* Compare ratios without multiplication/overflow; saturated input safe. */
    if (diff<mag/2U) return 0U;
    if (diff>mag+mag/2U) return 2U;
    return 1U;
}
uint8_t G1Pref_Feedback(G1PreferenceLearner *m,uint8_t source,
                        uint8_t level,uint8_t band,G1PrefAction action,
                        uint32_t tick) {
    const int index=idx(source,level,band);
    if (!m||index<0||action==G1_PREF_NEUTRAL||
        (action!=G1_PREF_UP && action!=G1_PREF_DOWN)) return 0U;
    if (!m->enabled) {m->ignored++;return 0U;}
    G1PrefCell *cell=&m->cells[index];
    if (cell->feedback!=0U && (uint32_t)(tick-cell->last_tick)<PREF_MIN_FEEDBACK_INTERVAL_MS) {
        m->ignored++;return 0U;
    }
    int value=(int)cell->preference+(int)action;
    if (value>PREF_MAX_BIAS) value=PREF_MAX_BIAS;
    if (value< -PREF_MAX_BIAS) value= -PREF_MAX_BIAS;
    cell->preference=(int8_t)value;
    if (cell->feedback<PREF_MAX_COUNT) cell->feedback++;
    cell->last_tick=tick;
    m->accepted++;
    return 1U;
}
uint32_t G1Pref_RecommendQ15(const G1PreferenceLearner *m,uint8_t source,
                            uint8_t level,uint8_t band,uint8_t *confidence) {
    if (confidence) *confidence=0U;
    const int index=idx(source,level,band);
    if (!m||!m->enabled||index<0) return G1_PREF_UNITY_Q15;
    const G1PrefCell *cell=&m->cells[index];
    if (confidence) *confidence=cell->feedback;
    if (cell->feedback<PREF_MIN_EVIDENCE) return G1_PREF_UNITY_Q15;
    /* Negative feedback represents wanting *less* volume. Positive asks for
     * less attenuation, but will never amplify beyond the input signal. */
    int bias=(int)cell->preference;
    if (bias>=0) return G1_PREF_UNITY_Q15;
    uint32_t reduction=(uint32_t)(-bias)*1024U;
    uint32_t result=G1_PREF_UNITY_Q15-reduction;
    return result<PREF_MIN_Q15?PREF_MIN_Q15:result;
}
