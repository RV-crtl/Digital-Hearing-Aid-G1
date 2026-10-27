#include "adaptive_preferences.h"
#include <assert.h>
#include <limits.h>
#include <stdio.h>
int main(void){
    G1AdaptivePreferences a;
    G1Pref_Init(&a);
    for(unsigned i=0;i<G1_PREF_BUCKET_COUNT;i++){
        assert(a.gain_db_q8[i]==0);
        assert(a.feedback_events[i]==0);
    }
    assert(G1Pref_Observe(&a,G1_PREF_LOUD,G1_PREF_HIGH));
    assert(G1Pref_Get(&a,G1_PREF_LOUD,G1_PREF_HIGH)==0);
    assert(G1Pref_Feedback(&a,G1_PREF_QUIET,G1_PREF_LOW,G1_PREF_INCREASE));
    assert(G1Pref_Get(&a,G1_PREF_QUIET,G1_PREF_LOW)>0);
    assert(G1Pref_Get(&a,G1_PREF_LOUD,G1_PREF_HIGH)==0);
    assert(!G1Pref_Feedback(&a,(G1PrefAmplitude)7,G1_PREF_HIGH,G1_PREF_INCREASE));
    assert(!G1Pref_Feedback(&a,G1_PREF_QUIET,G1_PREF_LOW,(G1PrefAction)0));
    for(int i=0;i<20000;i++){
        assert(G1Pref_Feedback(&a,G1_PREF_QUIET,G1_PREF_LOW,G1_PREF_INCREASE));
    }
    assert(G1Pref_Get(&a,G1_PREF_QUIET,G1_PREF_LOW)==G1_PREF_MAX_GAIN_DB_Q8);
    for(int i=0;i<20000;i++)
        assert(G1Pref_Feedback(&a,G1_PREF_QUIET,G1_PREF_LOW,G1_PREF_DECREASE));
    assert(G1Pref_Get(&a,G1_PREF_QUIET,G1_PREF_LOW)==G1_PREF_MIN_GAIN_DB_Q8);
    a.observations[0]=UINT16_MAX;
    assert(G1Pref_Observe(&a,G1_PREF_QUIET,G1_PREF_LOW));
    assert(a.observations[0]==UINT16_MAX);
    assert(!G1Pref_Observe(NULL,G1_PREF_QUIET,G1_PREF_LOW));
    assert(G1Pref_Get(NULL,G1_PREF_QUIET,G1_PREF_LOW)==0);
    puts("ADAPTIVE PREFERENCES: explicit-feedback-only bounded contextual learner PASS");
    return 0;
}
