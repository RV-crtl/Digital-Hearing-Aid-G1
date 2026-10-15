#include "speaker_output.h"
#include <stddef.h>

SAI_HandleTypeDef hspeaker_sai2b;
DMA_HandleTypeDef hdma_speaker_sai2b;

static uint8_t g_speaker_output_initialized = 0U;

HAL_StatusTypeDef SpeakerOutput_Init(void)
{
  GPIO_InitTypeDef gpio = {0};
  HAL_StatusTypeDef st;

  if (g_speaker_output_initialized != 0U) {
    return HAL_OK;
  }

  /* SAI2A is already initialised by CubeMX, but enable the shared SAI2 clock
   * explicitly so this module remains self-contained and regeneration-safe. */
  __HAL_RCC_SAI2_CLK_ENABLE();
  __HAL_RCC_GPIOA_CLK_ENABLE();
  __HAL_RCC_GPIOC_CLK_ENABLE();
  __HAL_RCC_DMA1_CLK_ENABLE();

  /* Final PCB output mapping: STM32 -> MAX98357 -> bone conductor. */
  gpio.Pin = GPIO_PIN_15;
  gpio.Mode = GPIO_MODE_AF_PP;
  gpio.Pull = GPIO_NOPULL;
  gpio.Speed = GPIO_SPEED_FREQ_LOW;
  gpio.Alternate = GPIO_AF13_SAI2;
  HAL_GPIO_Init(GPIOA, &gpio);

  gpio.Pin = GPIO_PIN_10 | GPIO_PIN_12;
  HAL_GPIO_Init(GPIOC, &gpio);

  /* Initialise SAI2 Block B as a 48-kHz, 16-bit stereo I2S master transmitter.
   * The mono processed sample is duplicated into L/R slots by demo_app.c. */
  hspeaker_sai2b.Instance = SAI2_Block_B;
  hspeaker_sai2b.Init.AudioMode = SAI_MODEMASTER_TX;
  hspeaker_sai2b.Init.Synchro = SAI_ASYNCHRONOUS;
  hspeaker_sai2b.Init.OutputDrive = SAI_OUTPUTDRIVE_ENABLE;
  hspeaker_sai2b.Init.NoDivider = SAI_MASTERDIVIDER_ENABLE;
  hspeaker_sai2b.Init.FIFOThreshold = SAI_FIFOTHRESHOLD_HF;
  hspeaker_sai2b.Init.AudioFrequency = SAI_AUDIO_FREQUENCY_48K;
  hspeaker_sai2b.Init.SynchroExt = SAI_SYNCEXT_DISABLE;
  hspeaker_sai2b.Init.MonoStereoMode = SAI_STEREOMODE;
  hspeaker_sai2b.Init.CompandingMode = SAI_NOCOMPANDING;
  hspeaker_sai2b.Init.TriState = SAI_OUTPUT_NOTRELEASED;

  st = HAL_SAI_InitProtocol(&hspeaker_sai2b,
                            SAI_I2S_STANDARD,
                            SAI_PROTOCOL_DATASIZE_16BIT,
                            2U);
  if (st != HAL_OK) {
    return st;
  }

  /* DMA1 Channel 7 is the valid SAI2_B path used by the earlier working output
   * implementation.  It is kept outside the IOC so CubeMX cannot drop it when
   * regenerating the otherwise-stable Bluetooth/telecoil project. */
  hdma_speaker_sai2b.Instance = DMA1_Channel7;
  hdma_speaker_sai2b.Init.Request = DMA_REQUEST_1;
  hdma_speaker_sai2b.Init.Direction = DMA_MEMORY_TO_PERIPH;
  hdma_speaker_sai2b.Init.PeriphInc = DMA_PINC_DISABLE;
  hdma_speaker_sai2b.Init.MemInc = DMA_MINC_ENABLE;
  hdma_speaker_sai2b.Init.PeriphDataAlignment = DMA_PDATAALIGN_HALFWORD;
  hdma_speaker_sai2b.Init.MemDataAlignment = DMA_MDATAALIGN_HALFWORD;
  hdma_speaker_sai2b.Init.Mode = DMA_CIRCULAR;
  hdma_speaker_sai2b.Init.Priority = DMA_PRIORITY_VERY_HIGH;

  st = HAL_DMA_Init(&hdma_speaker_sai2b);
  if (st != HAL_OK) {
    return st;
  }

  __HAL_LINKDMA(&hspeaker_sai2b, hdmatx, hdma_speaker_sai2b);

  /* Priority 5 is FreeRTOS-safe for HAL callbacks in this project. */
  HAL_NVIC_SetPriority(DMA1_Channel7_IRQn, 5U, 0U);
  HAL_NVIC_ClearPendingIRQ(DMA1_Channel7_IRQn);
  HAL_NVIC_EnableIRQ(DMA1_Channel7_IRQn);

  g_speaker_output_initialized = 1U;
  return HAL_OK;
}

HAL_StatusTypeDef SpeakerOutput_StartDma(int16_t *samples, uint16_t sample_count)
{
  HAL_StatusTypeDef st;

  if ((samples == NULL) || (sample_count == 0U)) {
    return HAL_ERROR;
  }

  st = SpeakerOutput_Init();
  if (st != HAL_OK) {
    return st;
  }

  return HAL_SAI_Transmit_DMA(&hspeaker_sai2b,
                              (uint8_t *)samples,
                              sample_count);
}

HAL_StatusTypeDef SpeakerOutput_StopDma(void)
{
  if (g_speaker_output_initialized == 0U) {
    return HAL_OK;
  }
  return HAL_SAI_DMAStop(&hspeaker_sai2b);
}

uint8_t SpeakerOutput_IsInitialized(void)
{
  return g_speaker_output_initialized;
}

/* The startup vector already names DMA1_Channel7_IRQHandler.  CubeMX does not
 * generate this ISR because Channel 7 is intentionally not present in the IOC,
 * so this application-owned handler survives Generate Code unchanged. */
void DMA1_Channel7_IRQHandler(void)
{
  HAL_DMA_IRQHandler(&hdma_speaker_sai2b);
}
