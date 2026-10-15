/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file    main.c
  * @brief   STM32L476RGT6 rubric demonstration - native FreeRTOS bootstrap.
  *
  * The proven USART2 path remains active.  FreeRTOS is started through the
  * native FreeRTOS API instead of CMSIS osKernelInitialize/osKernelStart.
  ******************************************************************************
  */
/* USER CODE END Header */

#include "main.h"
#include "dma.h"
#include "gpio.h"
#include "sai.h"
#include "usart.h"

/* USER CODE BEGIN Includes */
#include "demo_app.h"
#include "rtos_bootstrap.h"
#include "rtos_stats_timer.h"
#include "rtos_tick_proof.h"
/* USER CODE END Includes */

void SystemClock_Config(void);
void PeriphCommonClock_Config(void);

int main(void)
{
  HAL_Init();

  /* USER CODE BEGIN Init */
  Demo_EarlyInit();
  /* USER CODE END Init */

  SystemClock_Config();
  PeriphCommonClock_Config();

  MX_GPIO_Init();
  MX_DMA_Init();
  MX_USART2_UART_Init();
  MX_USART1_UART_Init();
  MX_SAI1_Init();
  MX_SAI2_Init();

  /* USER CODE BEGIN 2 */
  /* R30.3: initialise runtime profiling independently of FreeRTOS's optional
   * portCONFIGURE_TIMER_FOR_RUN_TIME_STATS() hook. TIM2 is not used by SAI,
   * DMA, speaker output, or the HAL tick (which uses TIM6).
   * Reinitialisation by the FreeRTOS startup hook happens before task runtime
   * accounting begins and is harmless; the driver is otherwise idempotent. */
  Demo_RuntimeStatsTimerInit();
  {
    const uint32_t t0 = Demo_RuntimeStatsTimerRead();
    HAL_Delay(2U); /* pre-scheduler; TIM6 HAL tick is already running */
    const uint32_t t1 = Demo_RuntimeStatsTimerRead();
    if ((uint32_t)(t1 - t0) < 500U) {
      Demo_ConsoleEarly("PROFILE: TIM2 NOT COUNTING at startup; inspect !PX after boot\r\n");
    } else {
      Demo_ConsoleEarly("PROFILE: TIM2 1MHz counter advancing at startup\r\n");
    }
  }
  Demo_SetStage(0x20U);
  Demo_ConsoleEarly("\r\nBOOT: USART2 PA2/PA3 -> NUCLEO ST-LINK VCP, 115200 8-N-1\r\n");
  Demo_ConsoleEarly("BOOT: 48 MHz core + UART + SAI1/SAI2A + SAI2B bone-output initialized; DMA is OFF\r\n");
  Demo_ConsoleEarly("BOOT: starting native FreeRTOS bootstrap with raw UART breadcrumbs\r\n");

  /* Does not return on a healthy system. */
  Demo_StartNativeRTOS();

  Demo_ConsolePanic("NATIVE_RTOS_RETURN", 0xF007U);
  /* USER CODE END 2 */

  while (1) { }
}

void SystemClock_Config(void)
{
  RCC_OscInitTypeDef RCC_OscInitStruct = {0};
  RCC_ClkInitTypeDef RCC_ClkInitStruct = {0};

  if (HAL_PWREx_ControlVoltageScaling(PWR_REGULATOR_VOLTAGE_SCALE1) != HAL_OK) Error_Handler();

  RCC_OscInitStruct.OscillatorType = RCC_OSCILLATORTYPE_HSE;
  RCC_OscInitStruct.HSEState = RCC_HSE_ON;
  RCC_OscInitStruct.PLL.PLLState = RCC_PLL_ON;
  RCC_OscInitStruct.PLL.PLLSource = RCC_PLLSOURCE_HSE;
  RCC_OscInitStruct.PLL.PLLM = 4;
  RCC_OscInitStruct.PLL.PLLN = 24;
  RCC_OscInitStruct.PLL.PLLP = RCC_PLLP_DIV7;
  RCC_OscInitStruct.PLL.PLLQ = RCC_PLLQ_DIV2;
  RCC_OscInitStruct.PLL.PLLR = RCC_PLLR_DIV2;
  if (HAL_RCC_OscConfig(&RCC_OscInitStruct) != HAL_OK) Error_Handler();

  RCC_ClkInitStruct.ClockType = RCC_CLOCKTYPE_HCLK | RCC_CLOCKTYPE_SYSCLK |
                                RCC_CLOCKTYPE_PCLK1 | RCC_CLOCKTYPE_PCLK2;
  RCC_ClkInitStruct.SYSCLKSource = RCC_SYSCLKSOURCE_PLLCLK;
  RCC_ClkInitStruct.AHBCLKDivider = RCC_SYSCLK_DIV1;
  RCC_ClkInitStruct.APB1CLKDivider = RCC_HCLK_DIV1;
  RCC_ClkInitStruct.APB2CLKDivider = RCC_HCLK_DIV1;
  if (HAL_RCC_ClockConfig(&RCC_ClkInitStruct, FLASH_LATENCY_2) != HAL_OK) Error_Handler();
}

void PeriphCommonClock_Config(void)
{
  RCC_PeriphCLKInitTypeDef PeriphClkInit = {0};

  PeriphClkInit.PeriphClockSelection = RCC_PERIPHCLK_SAI1 | RCC_PERIPHCLK_SAI2;
  PeriphClkInit.Sai1ClockSelection = RCC_SAI1CLKSOURCE_PLLSAI1;
  PeriphClkInit.Sai2ClockSelection = RCC_SAI2CLKSOURCE_PLLSAI1;
  PeriphClkInit.PLLSAI1.PLLSAI1Source = RCC_PLLSOURCE_HSE;
  PeriphClkInit.PLLSAI1.PLLSAI1M = 4;
  PeriphClkInit.PLLSAI1.PLLSAI1N = 43;
  PeriphClkInit.PLLSAI1.PLLSAI1P = RCC_PLLP_DIV7;
  PeriphClkInit.PLLSAI1.PLLSAI1Q = RCC_PLLQ_DIV4;
  PeriphClkInit.PLLSAI1.PLLSAI1R = RCC_PLLR_DIV2;
  PeriphClkInit.PLLSAI1.PLLSAI1ClockOut = RCC_PLLSAI1_SAI1CLK;
  if (HAL_RCCEx_PeriphCLKConfig(&PeriphClkInit) != HAL_OK) Error_Handler();
}

void HAL_TIM_PeriodElapsedCallback(TIM_HandleTypeDef *htim)
{
  if (htim->Instance == TIM6) {
    HAL_IncTick();
    /* Record the interrupted FreeRTOS task.  1 sample/ms, bounded work.
     * Works even if CubeMX regenerates FreeRTOSConfig.h without trace hooks. */
    Demo_TickProfilerSample();
  }
}

void Error_Handler(void)
{
  if (g_demo_diag.error_code == 0U) Demo_SetError(0xE001U);
  Demo_ConsolePanic("ERROR", g_demo_diag.error_code);
  __disable_irq();
  while (1) { }
}

#ifdef USE_FULL_ASSERT
void assert_failed(uint8_t *file, uint32_t line)
{
  (void)file; (void)line;
}
#endif
