#ifndef __SAI_H__
#define __SAI_H__
#ifdef __cplusplus
extern "C" {
#endif
#include "main.h"
extern SAI_HandleTypeDef hsai_BlockA1;
extern SAI_HandleTypeDef hsai_BlockA2;
extern DMA_HandleTypeDef hdma_sai1_a;
extern DMA_HandleTypeDef hdma_sai2_a;
void MX_SAI1_Init(void);
void MX_SAI2_Init(void);
#ifdef __cplusplus
}
#endif
#endif
