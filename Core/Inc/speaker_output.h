#ifndef SPEAKER_OUTPUT_H
#define SPEAKER_OUTPUT_H

#include "main.h"
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * SAI2 Block B is intentionally application-managed rather than CubeMX-managed.
 * The working CubeMX baseline owns SAI1A, SAI2A and their DMA channels.  This
 * module adds the independent final-output path after generated initialisation:
 *
 *   PA15 = SAI2_FS_B
 *   PC10 = SAI2_SCK_B
 *   PC12 = SAI2_SD_B
 *   DMA1 Channel 7 = SAI2_B TX, circular, 16-bit
 */

extern SAI_HandleTypeDef hspeaker_sai2b;
extern DMA_HandleTypeDef hdma_speaker_sai2b;

HAL_StatusTypeDef SpeakerOutput_Init(void);
HAL_StatusTypeDef SpeakerOutput_StartDma(int16_t *samples, uint16_t sample_count);
HAL_StatusTypeDef SpeakerOutput_StopDma(void);
uint8_t SpeakerOutput_IsInitialized(void);

#ifdef __cplusplus
}
#endif

#endif /* SPEAKER_OUTPUT_H */
