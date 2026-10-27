#include "scene_features.h"
#include <math.h>
#include <stddef.h>
#include <stdint.h>
#include <limits.h>

/* 2 cos(2 pi k/256), for k=1,5,21 at exactly 48 kHz. */
static const double coeff[3] = {
    1.9993976373924085, 1.9849590691974200, 1.7401739822174230
};

static uint32_t clamp_energy(double value) {
    if (!(value>=0.0)) return 0U;
    if (value>=4294967295.0) return UINT32_MAX;
    return (uint32_t)value;
}
bool G1Scene_Extract(const int16_t *pcm, size_t samples, G1SceneFeatures *out) {
    if (!pcm || !out || samples!=G1_SCENE_SAMPLES) return false;
    G1SceneFeatures f={0};
    uint64_t squares=0U;
    double q1[3]={0}, q2[3]={0};
    for (size_t i=0U;i<samples;i++) {
        const int32_t v=pcm[i];
        squares+=(uint64_t)(v*v);
        for(unsigned k=0U;k<3U;k++) {
            const double next=(double)v+coeff[k]*q1[k]-q2[k];
            q2[k]=q1[k]; q1[k]=next;
        }
    }
    f.rms_pcm=(uint32_t)sqrt((double)squares/(double)samples);
    uint32_t energy[3];
    for(unsigned k=0U;k<3U;k++) {
        const double e=q1[k]*q1[k]+q2[k]*q2[k]-coeff[k]*q1[k]*q2[k];
        energy[k]=clamp_energy(e/((double)samples*(double)samples));
    }
    f.low_energy=energy[0]; f.mid_energy=energy[1]; f.high_energy=energy[2];
    unsigned greatest=0U;
    if(energy[1]>energy[greatest])greatest=1U;
    if(energy[2]>energy[greatest])greatest=2U;
    f.spectrum=(G1PrefSpectrum)greatest;
    f.amplitude=(f.rms_pcm<900U)?G1_PREF_QUIET:
                (f.rms_pcm<6000U)?G1_PREF_MODERATE:G1_PREF_LOUD;
    *out=f;
    return true;
}
