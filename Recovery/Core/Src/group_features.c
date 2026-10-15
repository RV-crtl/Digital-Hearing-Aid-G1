#include "group_features.h"
#include <stdint.h>
#include <limits.h>
#include <string.h>

static int16_t sat16(int32_t x) {
    if (x > INT16_MAX) return INT16_MAX;
    if (x < INT16_MIN) return INT16_MIN;
    return (int16_t)x;
}
static uint32_t profile_gain(unsigned p) {
    /* Unity or attenuation only: conserves output headroom. */
    static const uint32_t q15[4] = {32768U, 31130U, 32768U, 27853U};
    return q15[(p <= 3U)?p:0U];
}
void GroupFeatures_Init(GroupFeatures *f) {
    if (!f) return;
    *f = (GroupFeatures){0};
    f->profile = GROUP_PROFILE_NEUTRAL;
    f->gain_q15 = f->target_gain_q15 = 32768U;
}
void GroupFeatures_Select(GroupFeatures *f, GroupProfile p) {
    if (!f || (unsigned)p > 3U) return;
    f->profile = (uint8_t)p;
    f->target_gain_q15 = profile_gain((unsigned)p);
    f->automatic = 0U;
    f->wind_lp = 0;
}
void GroupFeatures_Enable(GroupFeatures *f, uint8_t on) {
    if (!f) return;
    f->enabled = !!on;
    if (on) {
        /* Fade from unity when the effect is first enabled. */
        f->gain_q15 = 32768U;
        f->wind_lp = 0;
    }
}
void GroupFeatures_Auto(GroupFeatures *f, uint8_t on) {
    if (!f) return;
    f->automatic = !!on;
    f->candidate_count = 0U;
}
const char *GroupFeatures_ProfileName(unsigned p) {
    static const char *const names[] = {"NEUTRAL", "VOICE", "MUSIC", "WIND"};
    return (p < 4U) ? names[p] : "UNKNOWN";
}
const char *GroupFeatures_SceneName(unsigned p) {
    static const char *const names[] = {"QUIET", "MODERATE", "LOUD"};
    return (p < 3U) ? names[p] : "UNKNOWN";
}
void GroupFeatures_Process(GroupFeatures *f, const int16_t *in, int16_t *out, size_t n) {
    if (!f || !in || !out || n == 0U) return;
    if (!f->enabled) {
        /* Bypass is a true identity transform even for distinct buffers. */
        if (in != out) memmove(out, in, n * sizeof(*out));
        return;
    }
    /* Estimate input loudness, with bounded processing and no FFT/heap. */
    uint64_t mag = 0U;
    for (size_t i=0U; i<n; ++i) {
        int32_t s=in[i];
        mag += (uint32_t)(s < 0 ? -s : s);
    }
    f->recent_mean_abs = (uint32_t)(mag/n);
    if ((++f->processed_blocks & 31U)==0U) {
        uint8_t scene = (f->recent_mean_abs < 240U) ? GROUP_SCENE_QUIET :
                        (f->recent_mean_abs > 7500U) ? GROUP_SCENE_LOUD : GROUP_SCENE_MODERATE;
        if (scene == f->candidate) {
            if (f->candidate_count < 5U) f->candidate_count++;
        } else { f->candidate=scene; f->candidate_count=1U; }
        if (f->candidate_count >= 4U && scene != f->scene) {
            f->scene=scene;
            f->scene_updates++;
        }
        /* A deliberately transparent 3-level heuristic, NOT trained TinyML.
         * Auto NEVER selects MUSIC or WIND from volume alone. */
        if (f->automatic) {
            f->target_gain_q15=(f->scene==GROUP_SCENE_LOUD)?27853U:32768U;
        }
    }
    for (size_t i=0U; i<n; ++i) {
        if (f->gain_q15 < f->target_gain_q15) {
            uint32_t delta=f->target_gain_q15-f->gain_q15;
            f->gain_q15 += (delta>4U)?4U:delta;
        } else if (f->gain_q15 > f->target_gain_q15) {
            uint32_t delta=f->gain_q15-f->target_gain_q15;
            f->gain_q15 -= (delta>4U)?4U:delta;
        }
        int32_t sample=in[i];
        if (!f->automatic && f->profile==GROUP_PROFILE_WIND) {
            /* Simple high-pass (~100 Hz at 48 kHz); user-selected only.
             * LP state in Q0 samples; coefficient 420/32768. */
            f->wind_lp += ((sample-f->wind_lp)*420)/32768;
            sample -= f->wind_lp;
        }
        int32_t y=(int32_t)(((int64_t)sample*f->gain_q15)/32768);
        int16_t limited=sat16(y);
        if ((int32_t)limited!=y) f->saturated_samples++;
        out[i]=limited;
    }
}
