/**
 * @file g1_scene_ml.h
 * @brief Online acoustic context clustering, no training data or heap required.
 *
 * This is genuine incremental unsupervised learning of three acoustic feature
 * prototypes, NOT supervised classification of vehicles, speech, or hazards.
 * Labels are cluster indices; no acoustic-event class is claimed. Runtime is
 * bounded O(3) per block, with AudioTask as the sole state owner.
 */
#ifndef G1_SCENE_ML_H
#define G1_SCENE_ML_H
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
#define G1_SCENE_CLUSTERS 3U
#define G1_SCENE_FEATURES 3U
typedef struct {
  uint16_t centre[G1_SCENE_CLUSTERS][G1_SCENE_FEATURES];
  uint16_t observations[G1_SCENE_CLUSTERS];
  uint32_t trained_blocks;
  uint8_t last_cluster;
  uint8_t confidence_pct; /* separation margin; not a calibrated probability */
} G1SceneLearner;
void G1Scene_Init(G1SceneLearner *model);
/* Three bounded features: log2 energy, ZCR and crest factor, each 0..255. */
uint8_t G1Scene_Observe(G1SceneLearner *model, uint8_t energy,
                        uint8_t zcr, uint8_t crest);
#ifdef __cplusplus
}
#endif
#endif
