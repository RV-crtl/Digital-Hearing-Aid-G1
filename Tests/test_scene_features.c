#include "scene_features.h"
#include <assert.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif
static void tone(int16_t* samples,unsigned bin,int amplitude) {
    for(unsigned i=0;i<G1_SCENE_SAMPLES;i++)
        samples[i]=(int16_t)(amplitude*sin(2.0*M_PI*bin*i/G1_SCENE_SAMPLES));
}
int main(void) {
    int16_t buffer[G1_SCENE_SAMPLES]={0};
    G1SceneFeatures f;
    assert(!G1Scene_Extract(NULL,256,&f));
    assert(!G1Scene_Extract(buffer,255,&f));
    assert(!G1Scene_Extract(buffer,256,NULL));
    assert(G1Scene_Extract(buffer,256,&f));
    assert(f.rms_pcm==0 && f.low_energy==0 && f.spectrum==G1_PREF_LOW);
    tone(buffer,1U,2000);
    assert(G1Scene_Extract(buffer,256,&f));
    assert(f.spectrum==G1_PREF_LOW && f.amplitude==G1_PREF_MODERATE);
    tone(buffer,5U,12000);
    assert(G1Scene_Extract(buffer,256,&f));
    assert(f.spectrum==G1_PREF_MID && f.amplitude==G1_PREF_LOUD);
    tone(buffer,21U,100);
    assert(G1Scene_Extract(buffer,256,&f));
    assert(f.spectrum==G1_PREF_HIGH && f.amplitude==G1_PREF_QUIET);
    for(int i=0;i<256;i++) buffer[i]=(i%2)?32767:-32768;
    assert(G1Scene_Extract(buffer,256,&f));
    assert(f.rms_pcm>=32767U);
    puts("SCENE FEATURES: silence, DFT frequency bins, amplitudes, invalid lengths PASS");
    return 0;
}
