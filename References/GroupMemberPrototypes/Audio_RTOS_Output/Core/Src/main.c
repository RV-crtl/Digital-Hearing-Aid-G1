/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file           : main.c
  * @brief          : Sine wave output — 1 kHz, amplitude 1000, via SAI2+DMA
  *                   Hardware: STM32 NUCLEO-L476RG + MAX98357 I2S Amplifier
  ******************************************************************************
  */
/* USER CODE END Header */
/* Includes ------------------------------------------------------------------*/
#include "main.h"
#include "cmsis_os.h"

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */
#include <stdio.h>
#include <string.h>
#include <ctype.h>

#define MAX_MESSAGE_LEN 100
typedef char UARTMessage_t[MAX_MESSAGE_LEN];

/* USER CODE END Includes */

/* Private typedef -----------------------------------------------------------*/
/* USER CODE BEGIN PTD */

/* USER CODE END PTD */

/* Private define ------------------------------------------------------------*/
/* USER CODE BEGIN PD */

/* USER CODE END PD */

/* Private macro -------------------------------------------------------------*/
/* USER CODE BEGIN PM */

/* USER CODE END PM */

/* Private variables ---------------------------------------------------------*/
SAI_HandleTypeDef hsai_BlockB2;
DMA_HandleTypeDef hdma_sai2_b;

UART_HandleTypeDef huart2;

/* Definitions for SineWaveTask */
osThreadId_t SineWaveTaskHandle;
const osThreadAttr_t SineWaveTask_attributes = {
  .name = "SineWaveTask",
  .stack_size = 256 * 4,
  .priority = (osPriority_t) osPriorityNormal,
};
/* Definitions for StartBackground */
osThreadId_t StartBackgroundHandle;
const osThreadAttr_t StartBackground_attributes = {
  .name = "StartBackground",
  .stack_size = 128 * 4,
  .priority = (osPriority_t) osPriorityLow,
};
/* Definitions for ChirpWaveTask */
osThreadId_t ChirpWaveTaskHandle;
const osThreadAttr_t ChirpWaveTask_attributes = {
  .name = "ChirpWaveTask",
  .stack_size = 256 * 4,
  .priority = (osPriority_t) osPriorityNormal,
};
/* Definitions for UARTTask */
osThreadId_t UARTTaskHandle;
const osThreadAttr_t UARTTask_attributes = {
  .name = "UARTTask",
  .stack_size = 256 * 4,
  .priority = (osPriority_t) osPriorityBelowNormal,
};
/* Definitions for InputTask */
osThreadId_t InputTaskHandle;
const osThreadAttr_t InputTask_attributes = {
  .name = "InputTask",
  .stack_size = 512 * 4,
  .priority = (osPriority_t) osPriorityBelowNormal,
};
/* Definitions for UARTQueue */
osMessageQueueId_t UARTQueueHandle;
const osMessageQueueAttr_t UARTQueue_attributes = {
  .name = "UARTQueue"
};
/* USER CODE BEGIN PV */

/* USER CODE END PV */

/* Private function prototypes -----------------------------------------------*/
void SystemClock_Config(void);
static void MX_GPIO_Init(void);
static void MX_DMA_Init(void);
static void MX_USART2_UART_Init(void);
static void MX_SAI2_Init(void);
void StartSineWaveTask(void *argument);
void StartBackgroundTask(void *argument);
void StartChirpWaveTask(void *argument);
void StartUARTTask(void *argument);
void StartInputTask(void *argument);

/* USER CODE BEGIN PFP */
void GetUI(void);
void UART_Send(const char *text);
void GenerateSineWave(int SINE_FREQ, int AMPLITUDE);
void GenerateChirpWave(int startFreq, int endFreq, int amplitude);
uint8_t UI_ReadChar(char *c);

#define FLAG_NEW_SINE  0x1
#define FLAG_CHIRP     0x1

/* Settings typed by the user, read by the wave tasks */
volatile int sineFrequency  = 1000;
volatile int sineAmplitude  = 1000;
volatile int chirpStart     = 300;
volatile int chirpEnd       = 20000;
volatile int chirpAmplitude = 1000;

/* Characters received by the USART2 interrupt, waiting for InputTask */
osMessageQueueId_t RxQueueHandle;
uint8_t rxByte;
/* USER CODE END PFP */

/* Private user code ---------------------------------------------------------*/
/* USER CODE BEGIN 0 */


#include <math.h>
#include <stdint.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

#define SAMPLE_RATE    48000
//#define SINE_FREQ      10000
//#define AMPLITUDE      2000
#define FRAMES   12000  //256

volatile float background_result = 0.0f;
volatile uint32_t background_complexity = 10;
volatile uint32_t background_counter = 0;

int16_t audioBuffer[FRAMES * 2];

/* Queue text for UARTTask to print */
void UART_Send(const char *text)
{
    UARTMessage_t msg;
    strncpy(msg, text, MAX_MESSAGE_LEN - 1);
    msg[MAX_MESSAGE_LEN - 1] = '\0';
    osMessageQueuePut(UARTQueueHandle, &msg, 0, osWaitForever);
}

static int Clamp(int value, int min, int max)
{
    if (value < min) return min;
    if (value > max) return max;
    return value;
}

/* Called repeatedly by InputTask. 'static' keeps the line between calls. */
void GetUI(void)
{
    static char rxBuffer[32];
    static int rxIndex = 0;
    char rxChar;
    char msg[MAX_MESSAGE_LEN];
    int a, b, c;

    if (!UI_ReadChar(&rxChar))
        return;

    char echo[2] = { rxChar, '\0' };
    UART_Send(echo);

    /* Still typing: store the character */
    if (rxChar != '\r' && rxChar != '\n')
    {
        if (rxIndex < (int)sizeof(rxBuffer) - 1)
            rxBuffer[rxIndex++] = rxChar;
        return;
    }

    /* Enter pressed: process the line */
    rxBuffer[rxIndex] = '\0';
    rxIndex = 0;

    if (rxBuffer[0] == '\0')                  /* empty line, e.g. LF after CR */
        return;

    if (rxBuffer[0] == 'c')                   /* c <start> <end> <amp> */
    {
        a = chirpStart;  b = chirpEnd;  c = chirpAmplitude;
        sscanf(rxBuffer + 1, "%d %d %d", &a, &b, &c);

        chirpStart     = Clamp(a, 200, 20000);
        chirpEnd       = Clamp(b, 200, 20000);
        chirpAmplitude = Clamp(c, 0, 30000);

        osThreadFlagsSet(ChirpWaveTaskHandle, FLAG_CHIRP);
        snprintf(msg, sizeof(msg), "\r\nChirp %d -> %d Hz, Amp %d\r\n",
                 chirpStart, chirpEnd, chirpAmplitude);
    }
    else if (rxBuffer[0] == 's' || isdigit((unsigned char)rxBuffer[0]))
    {                                         /* s <freq> <amp>, or just <freq> <amp> */
        const char *args = (rxBuffer[0] == 's') ? rxBuffer + 1 : rxBuffer;
        a = sineFrequency;  b = sineAmplitude;
        sscanf(args, "%d %d", &a, &b);

        sineFrequency = Clamp(a, 200, 20000);
        sineAmplitude = Clamp(b, 0, 30000);

        osThreadFlagsSet(SineWaveTaskHandle, FLAG_NEW_SINE);
        snprintf(msg, sizeof(msg), "\r\nSine %d Hz, Amp %d\r\n",
                 sineFrequency, sineAmplitude);
    }
    else
    {
        snprintf(msg, sizeof(msg),
                 "\r\nUse: s <freq> <amp>   or   c <start> <end> <amp>\r\n");
    }

    UART_Send(msg);
}

/* Generate 1 kHz sine wave */
void GenerateSineWave(int SINE_FREQ, int AMPLITUDE)
{
    for (int i = 0; i < FRAMES; i++)
    {
        float phase = (2.0f * M_PI * SINE_FREQ * i) / SAMPLE_RATE;
        int16_t sample = (int16_t)(AMPLITUDE*sinf(phase));

        audioBuffer[2*i] = sample;
        audioBuffer[2*i+1] = sample;
    }
}

void GenerateChirpWave(int startFreq,
                       int endFreq,
                       int amplitude)
{
    float phase = 0.0f;

    for (int i = 0; i < FRAMES; i++)
    {
        /* Sweep position from 0.0 to 1.0 */
        float t = (float)i / (float)FRAMES;

        /* Linear frequency sweep */
        float freq = startFreq +
                     ((endFreq - startFreq) * t);

        /* Phase increment */
        phase += 2.0f * M_PI * freq / SAMPLE_RATE;

        /* Wrap phase */
        if (phase > 2.0f * M_PI)
            phase -= 2.0f * M_PI;

        int16_t sample =
            (int16_t)(amplitude * sinf(phase));

        /* Stereo output */
        audioBuffer[2 * i]     = sample;
        audioBuffer[2 * i + 1] = sample;
    }
}

/* Wait for the next received character. The USART2 interrupt puts every byte
 * into RxQueue, so none are lost even when a whole line arrives at once. */
uint8_t UI_ReadChar(char *c)
{
    uint8_t byte;

    if (osMessageQueueGet(RxQueueHandle, &byte, NULL, osWaitForever) != osOK)
        return 0;

    *c = (char)byte;
    return 1;
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
  /* USER CODE BEGIN 2 */

  /* Play the default chirp until the user types a command */
  GenerateChirpWave(chirpStart, chirpEnd, chirpAmplitude);

  /* Start continuous DMA transmission */
  HAL_SAI_Transmit_DMA(&hsai_BlockB2,
                       (uint8_t*)audioBuffer,
                       FRAMES * 2);

  /* USER CODE END 2 */

  /* Init scheduler */
  osKernelInitialize();

  /* USER CODE BEGIN RTOS_MUTEX */
  /* add mutexes, ... */
  /* USER CODE END RTOS_MUTEX */

  /* USER CODE BEGIN RTOS_SEMAPHORES */
  /* add semaphores, ... */
  /* USER CODE END RTOS_SEMAPHORES */

  /* USER CODE BEGIN RTOS_TIMERS */
  /* start timers, add new ones, ... */
  /* USER CODE END RTOS_TIMERS */

  /* Create the queue(s) */
  /* creation of UARTQueue */
  UARTQueueHandle = osMessageQueueNew (16, sizeof(UARTMessage_t), &UARTQueue_attributes);

  /* USER CODE BEGIN RTOS_QUEUES */
  RxQueueHandle = osMessageQueueNew(64, sizeof(uint8_t), NULL);
  /* USER CODE END RTOS_QUEUES */

  /* Create the thread(s) */
  /* creation of SineWaveTask */
  SineWaveTaskHandle = osThreadNew(StartSineWaveTask, NULL, &SineWaveTask_attributes);

  /* creation of StartBackground */
  StartBackgroundHandle = osThreadNew(StartBackgroundTask, NULL, &StartBackground_attributes);

  /* creation of ChirpWaveTask */
  ChirpWaveTaskHandle = osThreadNew(StartChirpWaveTask, NULL, &ChirpWaveTask_attributes);

  /* creation of UARTTask */
  UARTTaskHandle = osThreadNew(StartUARTTask, NULL, &UARTTask_attributes);

  /* creation of InputTask */
  InputTaskHandle = osThreadNew(StartInputTask, NULL, &InputTask_attributes);

  /* USER CODE BEGIN RTOS_THREADS */
  /* add threads, ... */
  /* USER CODE END RTOS_THREADS */

  /* USER CODE BEGIN RTOS_EVENTS */
  /* add events, ... */
  /* USER CODE END RTOS_EVENTS */

  /* Start scheduler */
  osKernelStart();

  /* We should never get here as control is now taken by the scheduler */

  /* Infinite loop */
  /* USER CODE BEGIN WHILE */
  while (1)
  {
    /* USER CODE END WHILE */

    /* USER CODE BEGIN 3 */
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
  /* DMA1_Channel7_IRQn interrupt configuration */
  HAL_NVIC_SetPriority(DMA1_Channel7_IRQn, 5, 0);
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
/* USART2 received one byte: queue it for InputTask, then listen for the next */
void HAL_UART_RxCpltCallback(UART_HandleTypeDef *huart)
{
    if (huart == &huart2)
    {
        osMessageQueuePut(RxQueueHandle, &rxByte, 0, 0);
        HAL_UART_Receive_IT(&huart2, &rxByte, 1);
    }
}

/* An overrun or framing error stops reception: restart it */
void HAL_UART_ErrorCallback(UART_HandleTypeDef *huart)
{
    if (huart == &huart2)
    {
        HAL_UART_Receive_IT(&huart2, &rxByte, 1);
    }
}
/* USER CODE END 4 */

/* USER CODE BEGIN Header_StartSineWaveTask */
/**
  * @brief  Function implementing the SineWaveTask thread.
  * @param  argument: Not used
  * @retval None
  */
/* USER CODE END Header_StartSineWaveTask */
void StartSineWaveTask(void *argument)
{
  /* USER CODE BEGIN 5 */
	/* Infinite loop */
	/* StartSineWaveTask: sleep until GetUI() sends new settings */
	for(;;)
	{
		osThreadFlagsWait(FLAG_NEW_SINE, osFlagsWaitAny, osWaitForever);
		GenerateSineWave(sineFrequency, sineAmplitude);
	}
  /* USER CODE END 5 */
}

/* USER CODE BEGIN Header_StartBackgroundTask */
/**
* @brief Function implementing the StartBackground thread.
* @param argument: Not used
* @retval None
*/
/* USER CODE END Header_StartBackgroundTask */
void StartBackgroundTask(void *argument)
{
  /* USER CODE BEGIN StartBackgroundTask */

    for (;;)
    {
        float result = 0.0f;

        /*
         * Increasing workload:
         *
         * result = sum from i=1 to N of:
         *          (i^2 + 3i + 7) / (i + 1)
         *
         * N increases every cycle.
         */

        for (uint32_t i = 1; i <= background_complexity; i++)
        {
            float x = (float)i;

            result += ((x*x) + (3.0f*x) + 7.0f)/ (x + 1.0f);
        }

        background_result = result;

        background_counter++;

        background_complexity += 10;

        // Stop N growing forever
        if (background_complexity > 1000)
        {
            background_complexity = 10;
        }


    }

  /* USER CODE END StartBackgroundTask */
}

/* USER CODE BEGIN Header_StartChirpWaveTask */
/**
* @brief Function implementing the ChirpWaveTask thread.
* @param argument: Not used
* @retval None
*/
/* USER CODE END Header_StartChirpWaveTask */
void StartChirpWaveTask(void *argument)
{
  /* USER CODE BEGIN StartChirpWaveTask */
  /* Sleep until GetUI() sends new chirp settings */
  for(;;)
  {
    osThreadFlagsWait(FLAG_CHIRP, osFlagsWaitAny, osWaitForever);
    GenerateChirpWave(chirpStart, chirpEnd, chirpAmplitude);
  }
  /* USER CODE END StartChirpWaveTask */
}

/* USER CODE BEGIN Header_StartUARTTask */
/**
* @brief Function implementing the UARTTask thread.
* @param argument: Not used
* @retval None
*/
/* USER CODE END Header_StartUARTTask */
void StartUARTTask(void *argument)
{
  /* USER CODE BEGIN StartUARTTask */
  /* The only task that writes to the UART: print whatever is queued */
  static UARTMessage_t msg;
  for(;;)
  {
    osMessageQueueGet(UARTQueueHandle, &msg, NULL, osWaitForever);
    HAL_UART_Transmit(&huart2, (uint8_t *)msg, strlen(msg), HAL_MAX_DELAY);
  }
  /* USER CODE END StartUARTTask */
}

/* USER CODE BEGIN Header_StartInputTask */
/**
* @brief Function implementing the InputTask thread.
* @param argument: Not used
* @retval None
*/
/* USER CODE END Header_StartInputTask */
void StartInputTask(void *argument)
{
  /* USER CODE BEGIN StartInputTask */
  /* The only task that reads the UART */
  HAL_UART_Receive_IT(&huart2, &rxByte, 1);   /* start interrupt-driven receive */
  UART_Send("\r\nCommands: s <freq> <amp>   c <start> <end> <amp>\r\n");
  for(;;)
  {
    GetUI();   /* blocks until a character arrives */
  }
  /* USER CODE END StartInputTask */
}

/**
  * @brief  Period elapsed callback in non blocking mode
  * @note   This function is called  when TIM6 interrupt took place, inside
  * HAL_TIM_IRQHandler(). It makes a direct call to HAL_IncTick() to increment
  * a global variable "uwTick" used as application time base.
  * @param  htim : TIM handle
  * @retval None
  */
void HAL_TIM_PeriodElapsedCallback(TIM_HandleTypeDef *htim)
{
  /* USER CODE BEGIN Callback 0 */

  /* USER CODE END Callback 0 */
  if (htim->Instance == TIM6)
  {
    HAL_IncTick();
  }
  /* USER CODE BEGIN Callback 1 */

  /* USER CODE END Callback 1 */
}

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
