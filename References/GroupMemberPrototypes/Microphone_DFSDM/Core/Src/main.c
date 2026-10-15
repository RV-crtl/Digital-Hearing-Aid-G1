/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file           : main.c
  * @brief          : Live DFSDM microphone to SAI2B/MAX98357 audio
  *                   Hardware: STM32 NUCLEO-L476RG + MAX98357 I2S Amplifier
  ******************************************************************************
  */
/* USER CODE END Header */
/* Includes ------------------------------------------------------------------*/
#include "main.h"

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */
#include <stdint.h>
#include <stdio.h>
#include <string.h>
/* USER CODE END Includes */

/* Private typedef -----------------------------------------------------------*/
/* USER CODE BEGIN PTD */

/* USER CODE END PTD */

/* Private define ------------------------------------------------------------*/
/* USER CODE BEGIN PD */
#define MIC_BUFFER_SAMPLES 256U
#define AUDIO_FRAMES       256U
#define FIFO_SAMPLES       4096U
#define FIFO_MASK          (FIFO_SAMPLES - 1U)
#define START_SAMPLES      1024U
#define MIC_GAIN           1.0f
#define OUTPUT_LIMIT       3000 /* Conservative initial PCM peak limit. */
#define REPORT_PERIOD_MS   500U
/* USER CODE END PD */

/* Private macro -------------------------------------------------------------*/
/* USER CODE BEGIN PM */

/* USER CODE END PM */

/* Private variables ---------------------------------------------------------*/
DFSDM_Filter_HandleTypeDef hdfsdm1_filter0;
DFSDM_Channel_HandleTypeDef hdfsdm1_channel2;
DMA_HandleTypeDef hdma_dfsdm1_flt0;

SAI_HandleTypeDef hsai_BlockB2;
DMA_HandleTypeDef hdma_sai2_b;

UART_HandleTypeDef huart2;

/* USER CODE BEGIN PV */
static int32_t mic_buffer[MIC_BUFFER_SAMPLES];
static int16_t audioBuffer[AUDIO_FRAMES * 2U];
static int16_t sample_fifo[FIFO_SAMPLES];
static volatile uint32_t fifo_write, fifo_read;
static float resample_phase, resample_step;
static float mic_rate, output_rate;
static float dc_input_previous, dc_output_previous;
static volatile uint32_t mic_blocks, tx_blocks;
static volatile uint32_t mic_errors, sai_errors;
static volatile uint32_t fifo_overruns, fifo_underruns, clipped_samples;
static volatile uint32_t mic_peak;
/* Both DMA IRQs have priority 0 in this project: callbacks cannot nest. */
/* USER CODE END PV */

/* Private function prototypes -----------------------------------------------*/
void SystemClock_Config(void);
static void MX_GPIO_Init(void);
static void MX_DMA_Init(void);
static void MX_USART2_UART_Init(void);
static void MX_SAI2_Init(void);
static void MX_DFSDM1_Init(void);
/* USER CODE BEGIN PFP */
static void UART_Print(const char *text);
static void CaptureMicBlock(uint32_t start);
static void FillAudioHalf(uint32_t first_frame);
static void ConfigureResampling(void);
/* USER CODE END PFP */

/* Private user code ---------------------------------------------------------*/
/* USER CODE BEGIN 0 */
static void UART_Print(const char *text)
{
    (void)HAL_UART_Transmit(&huart2, (uint8_t *)text,
                           (uint16_t)strlen(text), 100U);
}

static void ConfigureResampling(void)
{
    /* Existing configuration: system-clock CKOUT, internal SPI clock,
       fast continuous conversion. First conversion includes filter settling. */
    mic_rate = (float)HAL_RCC_GetSysClockFreq() /
               ((float)hdfsdm1_channel2.Init.OutputClock.Divider *
                (float)hdfsdm1_filter0.Init.FilterParam.Oversampling *
                (float)hdfsdm1_filter0.Init.FilterParam.IntOversampling);
    uint32_t divider = hsai_BlockB2.Init.Mckdiv;
    uint32_t sai_clock = HAL_RCCEx_GetPeriphCLKFreq(RCC_PERIPHCLK_SAI2);
    /* STM32L476 with master divider enabled: MCLK = 256 * frame rate.
       MCKDIV=0 is the no-division case; otherwise clock divisor = 2*MCKDIV. */
    output_rate = (float)sai_clock /
                  (256.0f * (divider ? (2.0f * (float)divider) : 1.0f));
    if (mic_rate < 1.0f || output_rate < 1.0f) Error_Handler();
    resample_step = mic_rate / output_rate;
}

static void CaptureMicBlock(uint32_t start)
{
    uint32_t peak = 0U;
    for (uint32_t i = start; i < start + MIC_BUFFER_SAMPLES / 2U; ++i)
    {
        /* Full-word DMA reads FLTRDATAR. Low 8 bits contain metadata;
           signed audio occupies bits 31:8. Shift again for initial gain. */
        float x = (float)(mic_buffer[i] >> 8) * 0.5f;
        float y = x - dc_input_previous + 0.995f * dc_output_previous;
        dc_input_previous = x;
        dc_output_previous = y;
        y *= MIC_GAIN;
        uint32_t level = (uint32_t)(y < 0.0f ? -y : y);
        if (level > peak) peak = level;
        if (y > (float)OUTPUT_LIMIT) { y = OUTPUT_LIMIT; clipped_samples++; }
        if (y < -(float)OUTPUT_LIMIT) { y = -OUTPUT_LIMIT; clipped_samples++; }
        uint32_t w = fifo_write;
        if ((uint32_t)(w - fifo_read) >= FIFO_SAMPLES)
        {
            /* Drop new data if full; never overwrite unread samples. */
            fifo_overruns++;
            continue;
        }
        sample_fifo[w & FIFO_MASK] = (int16_t)y;
        __DMB();
        fifo_write = w + 1U;
    }
    if (peak > mic_peak) mic_peak = peak;
    mic_blocks++;
}

static void FillAudioHalf(uint32_t first_frame)
{
    uint32_t r = fifo_read;
    uint32_t w = fifo_write;
    float phase = resample_phase;
    /* Read only completed microphone data; refill only a completed TX half.
       Fractional interpolation compensates for the different clock rates. */
    for (uint32_t i = 0; i < AUDIO_FRAMES / 2U; ++i)
    {
        int16_t sample = 0;
        uint32_t advance = (uint32_t)(phase + resample_step);
        uint32_t required = advance > 1U ? advance : 2U;
        if ((uint32_t)(w - r) >= required)
        {
            float a = (float)sample_fifo[r & FIFO_MASK];
            float b = (float)sample_fifo[(r + 1U) & FIFO_MASK];
            sample = (int16_t)(a + (b - a) * phase);
            phase += resample_step;
            uint32_t consumed = (uint32_t)phase;
            r += consumed;
            phase -= (float)consumed;
        }
        else
        {
            fifo_underruns++;
            phase = 0.0f;
        }
        uint32_t slot = (first_frame + i) * 2U;
        audioBuffer[slot] = sample;
        audioBuffer[slot + 1U] = sample;
    }
    resample_phase = phase;
    __DMB();
    fifo_read = r;
}
/* USER CODE END 0 */

/**
  * @brief  The application entry point.
  * @retval int
  */
int main(void)
{

  /* USER CODE BEGIN 1 */

  /* USER CODE END 1 */

  /* MCU Configuration--------------------------------------------------------*/

  /* Reset of all peripherals, Initializes the Flash interface and the Systick. */
  HAL_Init();

  /* USER CODE BEGIN Init */

  /* USER CODE END Init */

  /* Configure the system clock */
  SystemClock_Config();

  /* USER CODE BEGIN SysInit */

  /* USER CODE END SysInit */

  /* Initialize all configured peripherals */
  MX_GPIO_Init();
  MX_DMA_Init();
  MX_USART2_UART_Init();
  MX_SAI2_Init();
  MX_DFSDM1_Init();
  /* USER CODE BEGIN 2 */
UART_Print("\r\nMIC -> DFSDM -> SAI2B -> MAX98357\r\n");
  ConfigureResampling();
  char message[180];
  snprintf(message, sizeof(message), "Capture ~%lu Hz; playback ~%lu Hz\r\n",
           (unsigned long)(mic_rate + 0.5f),
           (unsigned long)(output_rate + 0.5f));
  UART_Print(message);

  if (HAL_DFSDM_FilterRegularStart_DMA(&hdfsdm1_filter0, mic_buffer,
                                      MIC_BUFFER_SAMPLES) != HAL_OK)
  {
      UART_Print("ERROR: DFSDM DMA start failed\r\n");
      Error_Handler();
  }
  uint32_t wait_started = HAL_GetTick();
  while ((uint32_t)(fifo_write - fifo_read) < START_SAMPLES)
  {
      if ((uint32_t)(HAL_GetTick() - wait_started) > 1000U)
      {
          UART_Print("ERROR: microphone DMA did not fill startup buffer\r\n");
          Error_Handler();
      }
  }
  /* IRQs are briefly masked while both TX halves are filled before DMA starts. */
  uint32_t irq_state = __get_PRIMASK();
  __disable_irq();
  FillAudioHalf(0U);
  FillAudioHalf(AUDIO_FRAMES / 2U);
  __set_PRIMASK(irq_state);
  if (HAL_SAI_Transmit_DMA(&hsai_BlockB2, (uint8_t *)audioBuffer,
                           AUDIO_FRAMES * 2U) != HAL_OK)
  {
      UART_Print("ERROR: SAI DMA start failed\r\n");
      Error_Handler();
  }
  UART_Print("Live audio started. UART: 115200, 8N1.\r\n");
  uint32_t last_report = HAL_GetTick();
/* USER CODE END 2 */

  /* Infinite loop */
  /* USER CODE BEGIN WHILE */
  while (1)
  {
    /* USER CODE END WHILE */

    /* USER CODE BEGIN 3 */
if ((uint32_t)(HAL_GetTick() - last_report) >= REPORT_PERIOD_MS)
      {
          last_report = HAL_GetTick();
          uint32_t saved = __get_PRIMASK();
          __disable_irq();
          uint32_t mic = mic_blocks, tx = tx_blocks;
          uint32_t level = mic_peak;
          uint32_t fill = fifo_write - fifo_read;
          uint32_t under = fifo_underruns, over = fifo_overruns;
          uint32_t me = mic_errors, se = sai_errors, clip = clipped_samples;
          mic_peak = 0U;
          __set_PRIMASK(saved);
          snprintf(message, sizeof(message),
                   "mic=%lu tx=%lu peak=%lu fifo=%lu under=%lu over=%lu clip=%lu err=%lu/%lu\r\n",
                   (unsigned long)mic, (unsigned long)tx, (unsigned long)level,
                   (unsigned long)fill, (unsigned long)under, (unsigned long)over,
                   (unsigned long)clip, (unsigned long)me, (unsigned long)se);
          UART_Print(message);
          HAL_GPIO_TogglePin(LD2_GPIO_Port, LD2_Pin);
      }
  }
/* USER CODE END 3 */
}

/**
  * @brief System Clock Configuration
  * @retval None
  */
void SystemClock_Config(void)
{
  RCC_OscInitTypeDef RCC_OscInitStruct = {0};
  RCC_ClkInitTypeDef RCC_ClkInitStruct = {0};

  /** Configure the main internal regulator output voltage
  */
  if (HAL_PWREx_ControlVoltageScaling(PWR_REGULATOR_VOLTAGE_SCALE1) != HAL_OK)
  {
    Error_Handler();
  }

  /** Initializes the RCC Oscillators according to the specified parameters
  * in the RCC_OscInitTypeDef structure.
  */
  RCC_OscInitStruct.OscillatorType = RCC_OSCILLATORTYPE_HSI;
  RCC_OscInitStruct.HSIState = RCC_HSI_ON;
  RCC_OscInitStruct.HSICalibrationValue = RCC_HSICALIBRATION_DEFAULT;
  RCC_OscInitStruct.PLL.PLLState = RCC_PLL_ON;
  RCC_OscInitStruct.PLL.PLLSource = RCC_PLLSOURCE_HSI;
  RCC_OscInitStruct.PLL.PLLM = 4;
  RCC_OscInitStruct.PLL.PLLN = 40;
  RCC_OscInitStruct.PLL.PLLP = RCC_PLLP_DIV7;
  RCC_OscInitStruct.PLL.PLLQ = RCC_PLLQ_DIV2;
  RCC_OscInitStruct.PLL.PLLR = RCC_PLLR_DIV2;
  if (HAL_RCC_OscConfig(&RCC_OscInitStruct) != HAL_OK)
  {
    Error_Handler();
  }

  /** Initializes the CPU, AHB and APB buses clocks
  */
  RCC_ClkInitStruct.ClockType = RCC_CLOCKTYPE_HCLK|RCC_CLOCKTYPE_SYSCLK
                              |RCC_CLOCKTYPE_PCLK1|RCC_CLOCKTYPE_PCLK2;
  RCC_ClkInitStruct.SYSCLKSource = RCC_SYSCLKSOURCE_PLLCLK;
  RCC_ClkInitStruct.AHBCLKDivider = RCC_SYSCLK_DIV1;
  RCC_ClkInitStruct.APB1CLKDivider = RCC_HCLK_DIV1;
  RCC_ClkInitStruct.APB2CLKDivider = RCC_HCLK_DIV1;

  if (HAL_RCC_ClockConfig(&RCC_ClkInitStruct, FLASH_LATENCY_4) != HAL_OK)
  {
    Error_Handler();
  }
}

/**
  * @brief DFSDM1 Initialization Function
  * @param None
  * @retval None
  */
static void MX_DFSDM1_Init(void)
{

  /* USER CODE BEGIN DFSDM1_Init 0 */

  /* USER CODE END DFSDM1_Init 0 */

  /* USER CODE BEGIN DFSDM1_Init 1 */

  /* USER CODE END DFSDM1_Init 1 */
  hdfsdm1_filter0.Instance = DFSDM1_Filter0;
  hdfsdm1_filter0.Init.RegularParam.Trigger = DFSDM_FILTER_SW_TRIGGER;
  hdfsdm1_filter0.Init.RegularParam.FastMode = ENABLE;
  hdfsdm1_filter0.Init.RegularParam.DmaMode = ENABLE;
  hdfsdm1_filter0.Init.FilterParam.SincOrder = DFSDM_FILTER_FASTSINC_ORDER;
  hdfsdm1_filter0.Init.FilterParam.Oversampling = 67;
  hdfsdm1_filter0.Init.FilterParam.IntOversampling = 1;
  if (HAL_DFSDM_FilterInit(&hdfsdm1_filter0) != HAL_OK)
  {
    Error_Handler();
  }
  hdfsdm1_channel2.Instance = DFSDM1_Channel2;
  hdfsdm1_channel2.Init.OutputClock.Activation = ENABLE;
  hdfsdm1_channel2.Init.OutputClock.Selection = DFSDM_CHANNEL_OUTPUT_CLOCK_SYSTEM;
  hdfsdm1_channel2.Init.OutputClock.Divider = 25;
  hdfsdm1_channel2.Init.Input.Multiplexer = DFSDM_CHANNEL_EXTERNAL_INPUTS;
  hdfsdm1_channel2.Init.Input.DataPacking = DFSDM_CHANNEL_STANDARD_MODE;
  hdfsdm1_channel2.Init.Input.Pins = DFSDM_CHANNEL_SAME_CHANNEL_PINS;
  hdfsdm1_channel2.Init.SerialInterface.Type = DFSDM_CHANNEL_SPI_RISING;
  hdfsdm1_channel2.Init.SerialInterface.SpiClock = DFSDM_CHANNEL_SPI_CLOCK_INTERNAL;
  hdfsdm1_channel2.Init.Awd.FilterOrder = DFSDM_CHANNEL_FASTSINC_ORDER;
  hdfsdm1_channel2.Init.Awd.Oversampling = 1;
  hdfsdm1_channel2.Init.Offset = 0;
  hdfsdm1_channel2.Init.RightBitShift = 0x00;
  if (HAL_DFSDM_ChannelInit(&hdfsdm1_channel2) != HAL_OK)
  {
    Error_Handler();
  }
  if (HAL_DFSDM_FilterConfigRegChannel(&hdfsdm1_filter0, DFSDM_CHANNEL_2, DFSDM_CONTINUOUS_CONV_ON) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN DFSDM1_Init 2 */

  /* USER CODE END DFSDM1_Init 2 */

}

/**
  * @brief SAI2 Initialization Function
  * @param None
  * @retval None
  */
static void MX_SAI2_Init(void)
{

  /* USER CODE BEGIN SAI2_Init 0 */

  /* USER CODE END SAI2_Init 0 */

  /* USER CODE BEGIN SAI2_Init 1 */

  /* USER CODE END SAI2_Init 1 */
  hsai_BlockB2.Instance = SAI2_Block_B;
  hsai_BlockB2.Init.AudioMode = SAI_MODEMASTER_TX;
  hsai_BlockB2.Init.Synchro = SAI_ASYNCHRONOUS;
  hsai_BlockB2.Init.OutputDrive = SAI_OUTPUTDRIVE_ENABLE;
  hsai_BlockB2.Init.NoDivider = SAI_MASTERDIVIDER_ENABLE;
  hsai_BlockB2.Init.FIFOThreshold = SAI_FIFOTHRESHOLD_EMPTY;
  hsai_BlockB2.Init.AudioFrequency = SAI_AUDIO_FREQUENCY_48K;
  hsai_BlockB2.Init.SynchroExt = SAI_SYNCEXT_DISABLE;
  hsai_BlockB2.Init.MonoStereoMode = SAI_STEREOMODE;
  hsai_BlockB2.Init.CompandingMode = SAI_NOCOMPANDING;
  hsai_BlockB2.Init.TriState = SAI_OUTPUT_NOTRELEASED;
  if (HAL_SAI_InitProtocol(&hsai_BlockB2, SAI_I2S_STANDARD, SAI_PROTOCOL_DATASIZE_16BIT, 2) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN SAI2_Init 2 */

  /* USER CODE END SAI2_Init 2 */

}

/**
  * @brief USART2 Initialization Function
  * @param None
  * @retval None
  */
static void MX_USART2_UART_Init(void)
{

  /* USER CODE BEGIN USART2_Init 0 */

  /* USER CODE END USART2_Init 0 */

  /* USER CODE BEGIN USART2_Init 1 */

  /* USER CODE END USART2_Init 1 */
  huart2.Instance = USART2;
  huart2.Init.BaudRate = 115200;
  huart2.Init.WordLength = UART_WORDLENGTH_8B;
  huart2.Init.StopBits = UART_STOPBITS_1;
  huart2.Init.Parity = UART_PARITY_NONE;
  huart2.Init.Mode = UART_MODE_TX_RX;
  huart2.Init.HwFlowCtl = UART_HWCONTROL_NONE;
  huart2.Init.OverSampling = UART_OVERSAMPLING_16;
  huart2.Init.OneBitSampling = UART_ONE_BIT_SAMPLE_DISABLE;
  huart2.AdvancedInit.AdvFeatureInit = UART_ADVFEATURE_NO_INIT;
  if (HAL_UART_Init(&huart2) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN USART2_Init 2 */

  /* USER CODE END USART2_Init 2 */

}

/**
  * Enable DMA controller clock
  */
static void MX_DMA_Init(void)
{

  /* DMA controller clock enable */
  __HAL_RCC_DMA1_CLK_ENABLE();

  /* DMA interrupt init */
  /* DMA1_Channel4_IRQn interrupt configuration */
  HAL_NVIC_SetPriority(DMA1_Channel4_IRQn, 0, 0);
  HAL_NVIC_EnableIRQ(DMA1_Channel4_IRQn);
  /* DMA1_Channel7_IRQn interrupt configuration */
  HAL_NVIC_SetPriority(DMA1_Channel7_IRQn, 0, 0);
  HAL_NVIC_EnableIRQ(DMA1_Channel7_IRQn);

}

/**
  * @brief GPIO Initialization Function
  * @param None
  * @retval None
  */
static void MX_GPIO_Init(void)
{
  GPIO_InitTypeDef GPIO_InitStruct = {0};
  /* USER CODE BEGIN MX_GPIO_Init_1 */

  /* USER CODE END MX_GPIO_Init_1 */

  /* GPIO Ports Clock Enable */
  __HAL_RCC_GPIOC_CLK_ENABLE();
  __HAL_RCC_GPIOH_CLK_ENABLE();
  __HAL_RCC_GPIOA_CLK_ENABLE();
  __HAL_RCC_GPIOB_CLK_ENABLE();

  /*Configure GPIO pin Output Level */
  HAL_GPIO_WritePin(LD2_GPIO_Port, LD2_Pin, GPIO_PIN_RESET);

  /*Configure GPIO pin : B1_Pin */
  GPIO_InitStruct.Pin = B1_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_IT_FALLING;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  HAL_GPIO_Init(B1_GPIO_Port, &GPIO_InitStruct);

  /*Configure GPIO pin : LD2_Pin */
  GPIO_InitStruct.Pin = LD2_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
  HAL_GPIO_Init(LD2_GPIO_Port, &GPIO_InitStruct);

  /* USER CODE BEGIN MX_GPIO_Init_2 */

  /* USER CODE END MX_GPIO_Init_2 */
}

/* USER CODE BEGIN 4 */
void HAL_DFSDM_FilterRegConvHalfCpltCallback(DFSDM_Filter_HandleTypeDef *filter)
{
    if (filter == &hdfsdm1_filter0) CaptureMicBlock(0U);
}

void HAL_DFSDM_FilterRegConvCpltCallback(DFSDM_Filter_HandleTypeDef *filter)
{
    if (filter == &hdfsdm1_filter0) CaptureMicBlock(MIC_BUFFER_SAMPLES / 2U);
}

void HAL_DFSDM_FilterErrorCallback(DFSDM_Filter_HandleTypeDef *filter)
{
    if (filter == &hdfsdm1_filter0) mic_errors++;
}

void HAL_SAI_TxHalfCpltCallback(SAI_HandleTypeDef *sai)
{
    if (sai == &hsai_BlockB2) { FillAudioHalf(0U); tx_blocks++; }
}

void HAL_SAI_TxCpltCallback(SAI_HandleTypeDef *sai)
{
    if (sai == &hsai_BlockB2) { FillAudioHalf(AUDIO_FRAMES / 2U); tx_blocks++; }
}

void HAL_SAI_ErrorCallback(SAI_HandleTypeDef *sai)
{
    if (sai == &hsai_BlockB2) sai_errors++;
}
/* USER CODE END 4 */

/**
  * @brief  This function is executed in case of error occurrence.
  * @retval None
  */
void Error_Handler(void)
{
  /* USER CODE BEGIN Error_Handler_Debug */
  /* User can add his own implementation to report the HAL error return state */
  __disable_irq();
  while (1)
  {
  }
  /* USER CODE END Error_Handler_Debug */
}
#ifdef USE_FULL_ASSERT
/**
  * @brief  Reports the name of the source file and the source line number
  *         where the assert_param error has occurred.
  * @param  file: pointer to the source file name
  * @param  line: assert_param error line source number
  * @retval None
  */
void assert_failed(uint8_t *file, uint32_t line)
{
  /* USER CODE BEGIN 6 */
  /* User can add his own implementation to report the file name and line number,
     ex: printf("Wrong parameters value: file %s on line %d\r\n", file, line) */
  /* USER CODE END 6 */
}
#endif /* USE_FULL_ASSERT */
