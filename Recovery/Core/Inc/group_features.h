#ifndef GROUP_FEATURES_H
#define GROUP_FEATURES_H
#include <stdint.h>
#include <stddef.h>
/* Portable, non-allocating DSP profile and environmental-level observer.
 * Single owner: AudioTask.  Commands arrive from FreeRTOS queue. */
typedef enum { GROUP_PROFILE_NEUTRAL=0, GROUP_PROFILE_VOICE=1,
               GROUP_PROFILE_MUSIC=2, GROUP_PROFILE_WIND=3 } GroupProfile;
typedef enum { GROUP_SCENE_QUIET=0, GROUP_SCENE_MODERATE=1,
               GROUP_SCENE_LOUD=2 } GroupScene;
typedef struct {
    uint8_t enabled, automatic;
    uint8_t profile, scene;
    uint32_t gain_q15, target_gain_q15;
    uint32_t processed_blocks, scene_updates, saturated_samples;
    uint32_t recent_mean_abs;
    int32_t wind_lp;
    uint8_t candidate, candidate_count;
} GroupFeatures;
void GroupFeatures_Init(GroupFeatures *f);
void GroupFeatures_Select(GroupFeatures *f, GroupProfile profile);
void GroupFeatures_Enable(GroupFeatures *f, uint8_t on);
void GroupFeatures_Auto(GroupFeatures *f, uint8_t on);
/* in/out may alias. Inactive path does not touch samples. */
void GroupFeatures_Process(GroupFeatures *f, const int16_t *in, int16_t *out, size_t n);
const char *GroupFeatures_ProfileName(unsigned profile);
const char *GroupFeatures_SceneName(unsigned scene);
#endif
