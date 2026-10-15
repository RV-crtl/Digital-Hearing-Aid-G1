#include "sai.h"
SAI_HandleTypeDef hsai_BlockA1;
SAI_HandleTypeDef hsai_BlockA2;
DMA_HandleTypeDef hdma_sai1_a;
DMA_HandleTypeDef hdma_sai2_a;
static uint32_t SAI1_client = 0U;
static uint32_t SAI2_client = 0U;

void MX_SAI1_Init(void)
{
  hsai_BlockA1.Instance = SAI1_Block_A;
  hsai_BlockA1.Init.AudioMode = SAI_MODEMASTER_RX;
  hsai_BlockA1.Init.Synchro = SAI_ASYNCHRONOUS;
  hsai_BlockA1.Init.OutputDrive = SAI_OUTPUTDRIVE_ENABLE;
  hsai_BlockA1.Init.NoDivider = SAI_MASTERDIVIDER_ENABLE;
  hsai_BlockA1.Init.FIFOThreshold = SAI_FIFOTHRESHOLD_HF;
  hsai_BlockA1.Init.AudioFrequency = SAI_AUDIO_FREQUENCY_48K;
  /* Share the already-proven SAI1 master frame/bit timing internally with SAI2A.
   * This does not change the Bluetooth clock rate or audio configuration. */
  hsai_BlockA1.Init.SynchroExt = SAI_SYNCEXT_OUTBLOCKA_ENABLE;
  hsai_BlockA1.Init.MonoStereoMode = SAI_STEREOMODE;
  hsai_BlockA1.Init.CompandingMode = SAI_NOCOMPANDING;
  if (HAL_SAI_InitProtocol(&hsai_BlockA1, SAI_I2S_STANDARD, SAI_PROTOCOL_DATASIZE_24BIT, 2) != HAL_OK) Error_Handler();
}

void MX_SAI2_Init(void)
{
  hsai_BlockA2.Instance = SAI2_Block_A;
  hsai_BlockA2.Init.AudioMode = SAI_MODESLAVE_RX;
  /* U3/U4 PCM1808s share clocks.  SAI2A receives its timing from SAI1A
   * through STM32 cross-SAI synchronisation; PB15 is the separate U4 data. */
  hsai_BlockA2.Init.Synchro = SAI_SYNCHRONOUS_EXT_SAI1;
  hsai_BlockA2.Init.OutputDrive = SAI_OUTPUTDRIVE_DISABLE;
  hsai_BlockA2.Init.FIFOThreshold = SAI_FIFOTHRESHOLD_HF;
  hsai_BlockA2.Init.SynchroExt = SAI_SYNCEXT_DISABLE;
  hsai_BlockA2.Init.MonoStereoMode = SAI_STEREOMODE;
  hsai_BlockA2.Init.CompandingMode = SAI_NOCOMPANDING;
  hsai_BlockA2.Init.TriState = SAI_OUTPUT_NOTRELEASED;
  if (HAL_SAI_InitProtocol(&hsai_BlockA2, SAI_I2S_STANDARD, SAI_PROTOCOL_DATASIZE_24BIT, 2) != HAL_OK) Error_Handler();
}

void HAL_SAI_MspInit(SAI_HandleTypeDef *saiHandle)
{
  GPIO_InitTypeDef GPIO_InitStruct = {0};
  if (saiHandle->Instance == SAI1_Block_A)
  {
    if (SAI1_client++ == 0U) __HAL_RCC_SAI1_CLK_ENABLE();
    __HAL_RCC_GPIOB_CLK_ENABLE();
    __HAL_RCC_GPIOC_CLK_ENABLE();
    /* Final PCB: Bluetooth PCM1808 U3. PC3=SD, PB10=BCLK, PB8=MCLK, PB9=LRCLK. */
    GPIO_InitStruct.Pin = GPIO_PIN_3;
    GPIO_InitStruct.Mode = GPIO_MODE_AF_PP;
    GPIO_InitStruct.Pull = GPIO_NOPULL;
    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_VERY_HIGH;
    GPIO_InitStruct.Alternate = GPIO_AF13_SAI1;
    HAL_GPIO_Init(GPIOC, &GPIO_InitStruct);
    GPIO_InitStruct.Pin = GPIO_PIN_8 | GPIO_PIN_9 | GPIO_PIN_10;
    HAL_GPIO_Init(GPIOB, &GPIO_InitStruct);
    hdma_sai1_a.Instance = DMA2_Channel1;
    hdma_sai1_a.Init.Request = DMA_REQUEST_1;
    hdma_sai1_a.Init.Direction = DMA_PERIPH_TO_MEMORY;
    hdma_sai1_a.Init.PeriphInc = DMA_PINC_DISABLE;
    hdma_sai1_a.Init.MemInc = DMA_MINC_ENABLE;
    hdma_sai1_a.Init.PeriphDataAlignment = DMA_PDATAALIGN_WORD;
    hdma_sai1_a.Init.MemDataAlignment = DMA_MDATAALIGN_WORD;
    hdma_sai1_a.Init.Mode = DMA_CIRCULAR;
    hdma_sai1_a.Init.Priority = DMA_PRIORITY_VERY_HIGH;
    if (HAL_DMA_Init(&hdma_sai1_a) != HAL_OK) Error_Handler();
    __HAL_LINKDMA(saiHandle, hdmarx, hdma_sai1_a);
  }
  else if (saiHandle->Instance == SAI2_Block_A)
  {
    if (SAI2_client++ == 0U) __HAL_RCC_SAI2_CLK_ENABLE();
    __HAL_RCC_GPIOB_CLK_ENABLE();
    /* Telecoil U4: PB15 carries the unique SD input. Shared LRCLK/BCLK timing
     * is sourced internally from SAI1A rather than requiring clocks at PB12/13.
     * PB12/PB13 are retained untouched for board-revision compatibility. */
    GPIO_InitStruct.Pin = GPIO_PIN_12 | GPIO_PIN_13 | GPIO_PIN_15;
    GPIO_InitStruct.Mode = GPIO_MODE_AF_PP;
    GPIO_InitStruct.Pull = GPIO_NOPULL;
    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_VERY_HIGH;
    GPIO_InitStruct.Alternate = GPIO_AF13_SAI2;
    HAL_GPIO_Init(GPIOB, &GPIO_InitStruct);
    hdma_sai2_a.Instance = DMA1_Channel6;
    hdma_sai2_a.Init.Request = DMA_REQUEST_1;
    hdma_sai2_a.Init.Direction = DMA_PERIPH_TO_MEMORY;
    hdma_sai2_a.Init.PeriphInc = DMA_PINC_DISABLE;
    hdma_sai2_a.Init.MemInc = DMA_MINC_ENABLE;
    hdma_sai2_a.Init.PeriphDataAlignment = DMA_PDATAALIGN_WORD;
    hdma_sai2_a.Init.MemDataAlignment = DMA_MDATAALIGN_WORD;
    hdma_sai2_a.Init.Mode = DMA_CIRCULAR;
    hdma_sai2_a.Init.Priority = DMA_PRIORITY_VERY_HIGH;
    if (HAL_DMA_Init(&hdma_sai2_a) != HAL_OK) Error_Handler();
    __HAL_LINKDMA(saiHandle, hdmarx, hdma_sai2_a);
  }
}

void HAL_SAI_MspDeInit(SAI_HandleTypeDef *saiHandle)
{
  if (saiHandle->Instance == SAI1_Block_A)
  {
    if (SAI1_client > 0U) SAI1_client--;
    if (SAI1_client == 0U) __HAL_RCC_SAI1_CLK_DISABLE();
    HAL_GPIO_DeInit(GPIOC, GPIO_PIN_3);
    HAL_GPIO_DeInit(GPIOB, GPIO_PIN_8 | GPIO_PIN_9 | GPIO_PIN_10);
    HAL_DMA_DeInit(saiHandle->hdmarx);
  }
  else if (saiHandle->Instance == SAI2_Block_A)
  {
    if (SAI2_client > 0U) SAI2_client--;
    if (SAI2_client == 0U) __HAL_RCC_SAI2_CLK_DISABLE();
    HAL_GPIO_DeInit(GPIOB, GPIO_PIN_12 | GPIO_PIN_13 | GPIO_PIN_15);
    HAL_DMA_DeInit(saiHandle->hdmarx);
  }
}
