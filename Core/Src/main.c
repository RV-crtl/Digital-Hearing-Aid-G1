/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file    main.c
  * @brief   STM32L476RGT6 final PCB hardware application - native FreeRTOS.
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
#include "g1_product_board.h"
#include "g1_boot_diag.h"
#include "g1_operating_config.h"
#include <stdio.h>
/* USER CODE END Includes */

void SystemClock_Config(void);
void PeriphCommonClock_Config(void);

/* Stored before HAL_Init and before STM32 HAL/CubeMX code can clear RCC flags. */
static volatile uint32_t s_g1_boot_reset_csr;
static volatile uint32_t s_g1_boot_debug_attached;
static volatile uint32_t s_g1_boot_clock_fallback;
static volatile uint32_t s_g1_boot_hse_attempt;
static volatile uint32_t s_g1_boot_sysclk_hz;


int main(void)
{
  s_g1_boot_reset_csr=RCC->CSR;
  s_g1_boot_debug_attached=(CoreDebug->DHCSR & CoreDebug_DHCSR_C_DEBUGEN_Msk)?1U:0U;
  __HAL_RCC_CLEAR_RESET_FLAGS();
  HAL_Init();

  /* USER CODE BEGIN Init */
  Demo_EarlyInit();
  /* USER CODE END Init */

  SystemClock_Config();
  PeriphCommonClock_Config();

  MX_GPIO_Init();
  MX_DMA_Init();
  /* Battery-only operating build: NO USART2 init, no UART console, no ST-LINK.
   * Compile with G1_DIAGNOSTICS_UART_ENABLE=1 for a deliberate bench build.
   * Bluetooth USART1 still initializes normally in either configuration. */
#if G1_DIAGNOSTICS_UART_ENABLE
  MX_USART2_UART_Init();
#endif
  /* Boot snapshot belongs to the application module, which owns BOOT STATUS. */
  G1_BootCapture(s_g1_boot_reset_csr, s_g1_boot_debug_attached,
                 s_g1_boot_clock_fallback, s_g1_boot_hse_attempt,
                 s_g1_boot_sysclk_hz);
  G1_BootReport();
  MX_USART1_UART_Init();
  MX_SAI1_Init();
  MX_SAI2_Init();
  G1_BoardInit(); /* PDM mic, touch I2C and encoder on custom PCB */
  /* Emit the initialization outcome from hardware, not from assumed success.
   * This makes an unexecuted board initializer distinguishable from a DMA fault. */
  if (g_g1_board.mic_init_attempts == 0U) {
    Demo_ConsoleEarly("G1|BOOT|ERROR|board_mic_init_not_executed\r\n");
  } else if (g_g1_board.mic_ready) {
    Demo_ConsoleEarly("G1|BOOT|MIC|DFSDM_INIT_OK\r\n");
  } else {
    Demo_ConsoleEarly("G1|BOOT|MIC|DFSDM_INIT_FAILED|use_MIC_STATUS\r\n");
  }


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
  Demo_ConsoleEarly("\r\nG1 STANDALONE: optional USART2 test header, 115200 8-N-1\r\n");
  Demo_ConsoleEarly("G1: 48MHz core + BT + telecoil + PDM mic + touch + encoder + speaker\r\n");
  Demo_ConsoleEarly("BOOT: starting native FreeRTOS bootstrap with raw UART breadcrumbs\r\n");

  /* Does not return on a healthy system. */
  Demo_StartNativeRTOS();

  Demo_ConsolePanic("NATIVE_RTOS_RETURN", 0xF007U);
  /* USER CODE END 2 */

  while (1) { }
}

void SystemClock_Config(void)
{
  RCC_OscInitTypeDef osc = {0};
  RCC_ClkInitTypeDef clk = {0};
  HAL_StatusTypeDef st;

  if (HAL_PWREx_ControlVoltageScaling(PWR_REGULATOR_VOLTAGE_SCALE1) != HAL_OK)
    Error_Handler();

  /* Product normally runs from its populated 16-MHz HSE crystal. When the
   * oscillator fails during a cold battery boot, the old firmware entered
   * Error_Handler before UART was configured and appeared completely dead.
   * HSI16 has the same nominal frequency, allowing both 48-MHz system PLL
   * and PLLSAI1 to be configured while retaining the known working HSE path.
   * An unregulated supply, held NRST or BOOT0 cannot be fixed in firmware. */
  osc.OscillatorType=RCC_OSCILLATORTYPE_HSE;
  osc.HSEState=RCC_HSE_ON;
  osc.PLL.PLLState=RCC_PLL_ON;
  osc.PLL.PLLSource=RCC_PLLSOURCE_HSE;
  osc.PLL.PLLM=4;
  osc.PLL.PLLN=24;
  osc.PLL.PLLP=RCC_PLLP_DIV7;
  osc.PLL.PLLQ=RCC_PLLQ_DIV2;
  osc.PLL.PLLR=RCC_PLLR_DIV2;
  st=HAL_RCC_OscConfig(&osc);
  s_g1_boot_hse_attempt=(uint32_t)st;
  if(st!=HAL_OK) {
    RCC_OscInitTypeDef hsi = {0};
    s_g1_boot_clock_fallback=1U;
    /* The failing external oscillator must not be needed by the fallback. */
    hsi.OscillatorType=RCC_OSCILLATORTYPE_HSI;
    hsi.HSIState=RCC_HSI_ON;
    hsi.HSICalibrationValue=RCC_HSICALIBRATION_DEFAULT;
    hsi.PLL.PLLState=RCC_PLL_ON;
    hsi.PLL.PLLSource=RCC_PLLSOURCE_HSI;
    hsi.PLL.PLLM=4;
    hsi.PLL.PLLN=24;
    hsi.PLL.PLLP=RCC_PLLP_DIV7;
    hsi.PLL.PLLQ=RCC_PLLQ_DIV2;
    hsi.PLL.PLLR=RCC_PLLR_DIV2;
    if(HAL_RCC_OscConfig(&hsi)!=HAL_OK)Error_Handler();
  }
  clk.ClockType=RCC_CLOCKTYPE_HCLK|RCC_CLOCKTYPE_SYSCLK|
                RCC_CLOCKTYPE_PCLK1|RCC_CLOCKTYPE_PCLK2;
  clk.SYSCLKSource=RCC_SYSCLKSOURCE_PLLCLK;
  clk.AHBCLKDivider=RCC_SYSCLK_DIV1;
  clk.APB1CLKDivider=RCC_HCLK_DIV1;
  clk.APB2CLKDivider=RCC_HCLK_DIV1;
  if(HAL_RCC_ClockConfig(&clk,FLASH_LATENCY_2)!=HAL_OK)Error_Handler();
  s_g1_boot_sysclk_hz=HAL_RCC_GetSysClockFreq();
}

void PeriphCommonClock_Config(void)
{
  RCC_PeriphCLKInitTypeDef pclk={0};
  pclk.PeriphClockSelection=RCC_PERIPHCLK_SAI1|RCC_PERIPHCLK_SAI2;
  pclk.Sai1ClockSelection=RCC_SAI1CLKSOURCE_PLLSAI1;
  pclk.Sai2ClockSelection=RCC_SAI2CLKSOURCE_PLLSAI1;
  pclk.PLLSAI1.PLLSAI1Source=s_g1_boot_clock_fallback?RCC_PLLSOURCE_HSI:RCC_PLLSOURCE_HSE;
  pclk.PLLSAI1.PLLSAI1M=4;
  pclk.PLLSAI1.PLLSAI1N=43;
  pclk.PLLSAI1.PLLSAI1P=RCC_PLLP_DIV7;
  pclk.PLLSAI1.PLLSAI1Q=RCC_PLLQ_DIV4;
  pclk.PLLSAI1.PLLSAI1R=RCC_PLLR_DIV2;
  pclk.PLLSAI1.PLLSAI1ClockOut=RCC_PLLSAI1_SAI1CLK;
  if(HAL_RCCEx_PeriphCLKConfig(&pclk)!=HAL_OK)Error_Handler();
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
