#include "preference_learner.h"
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
int main(void) {
    G1PreferenceLearner learner;G1Pref_Init(&learner);
    uint8_t c=100U;
    assert(G1Pref_RecommendQ15(&learner,0,1,1,&c)==32768U && c==0U);
    assert(!G1Pref_Feedback(&learner,0,1,1,G1_PREF_DOWN,500U));
    assert(learner.ignored==1U);
    G1Pref_Enable(&learner,1U);
    assert(G1Pref_Feedback(&learner,0,1,1,G1_PREF_DOWN,1000U));
    assert(!G1Pref_Feedback(&learner,0,1,1,G1_PREF_DOWN,1100U)); /* cooldown */
    assert(G1Pref_RecommendQ15(&learner,0,1,1,&c)==32768U && c==1U);
    assert(G1Pref_Feedback(&learner,0,1,1,G1_PREF_DOWN,1300U));
    assert(G1Pref_RecommendQ15(&learner,0,1,1,&c)==30720U && c==2U);
    for(uint32_t i=0;i<30U;i++) assert(G1Pref_Feedback(&learner,0,1,1,G1_PREF_DOWN,1600U+300U*i));
    assert(G1Pref_RecommendQ15(&learner,0,1,1,&c)==24576U && c==32U);
    assert(G1Pref_RecommendQ15(&learner,1,1,1,&c)==32768U && c==0U);
    assert(G1Pref_Feedback(&learner,0,1,1,G1_PREF_UP,11000U));
    assert(G1Pref_RecommendQ15(&learner,0,1,1,&c)==25600U);
    assert(!G1Pref_Feedback(&learner,3,1,1,G1_PREF_UP,12000U));
    assert(!G1Pref_Feedback(&learner,0,3,1,G1_PREF_UP,12000U));
    assert(!G1Pref_Feedback(&learner,0,1,3,G1_PREF_UP,12000U));
    assert(!G1Pref_Feedback(&learner,0,1,1,G1_PREF_NEUTRAL,12000U));
    assert(G1Pref_LevelFromMean(0)==0 && G1Pref_LevelFromMean(240)==1 && G1Pref_LevelFromMean(7501)==2);
    const int16_t low[12]={1000,1000,1000,1000,1000,1000,1000,1000,1000,1000,1000,1000};
    const int16_t high[12]={-1000,1000,-1000,1000,-1000,1000,-1000,1000,-1000,1000,-1000,1000};
    assert(G1Pref_BandFromPCM(low,12)==0);
    assert(G1Pref_BandFromPCM(high,12)==2);
    assert(G1Pref_BandFromPCM(0,0)==1);
    G1Pref_Reset(&learner);
    assert(learner.enabled==1U && learner.accepted==0U);
    assert(G1Pref_RecommendQ15(&learner,0,1,1,&c)==32768U);
    puts("G1 preference learner: contextual feedback, confidence, cooldown, attenuation-only, reset PASS");
    return 0;
}
