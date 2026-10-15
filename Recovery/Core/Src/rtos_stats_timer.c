/* Independent TIM2 1 MHz FreeRTOS scheduler run-time timer.
 * TIM6 handles the HAL tick. TIM2 has no IRQ and is not used for audio.
 * R30.3: initialise explicitly at board boot and in the FreeRTOS hook;
 * the driver is idempotent after it has started and exposes recovery status.
 */
#include "main.h"
#include "rtos_stats_timer.h"

static volatile uint32_t s_init_calls;
static volatile uint32_t s_reenable_calls;

void Demo_RuntimeStatsTimerInit(void)
{
  RCC_ClkInitTypeDef clk = {0};
  uint32_t flash_latency = 0U;
  const uint32_t pclk = HAL_RCC_GetPCLK1Freq();
  HAL_RCC_GetClockConfig(&clk, &flash_latency);
  const uint32_t timer_clk = pclk *
    ((clk.APB1CLKDivider == RCC_HCLK_DIV1) ? 1UL : 2UL);
  s_init_calls++;
  if (timer_clk < 1000000UL) return;
  __HAL_RCC_TIM2_CLK_ENABLE();
  const uint32_t wanted_psc = timer_clk / 1000000UL - 1UL;

  /* FreeRTOS may call this hook a second time. Never reset the hardware
   * counter if it is already running at the correct divider: doing so after
   * tasks start would make their accumulated runtime figures discontinuous. */
  if (((TIM2->CR1 & TIM_CR1_CEN) != 0U) &&
      (TIM2->PSC == wanted_psc) &&
      (TIM2->ARR == 0xFFFFFFFFUL)) return;

  TIM2->CR1 = 0U;
  TIM2->PSC = wanted_psc;
  TIM2->ARR = 0xFFFFFFFFUL;
  TIM2->EGR = TIM_EGR_UG;
  TIM2->SR = 0U;
  TIM2->CNT = 0U;
  TIM2->CR1 = TIM_CR1_CEN;
}

/* Called only if the sampler observed a stalled counter. Re-enable a disabled
 * timer or disabled RCC clock while preserving CNT. Never reset a running
 * counter or manufacture CPU-utilisation measurements. */
uint32_t Demo_RuntimeStatsTimerEnsureRunning(void)
{
  uint32_t repaired = 0U;
  if ((RCC->APB1ENR1 & RCC_APB1ENR1_TIM2EN) == 0U) {
    __HAL_RCC_TIM2_CLK_ENABLE();
    repaired = 1U;
  }
  if ((TIM2->CR1 & TIM_CR1_CEN) == 0U) {
    TIM2->CR1 |= TIM_CR1_CEN;
    repaired = 1U;
  }
  if (repaired != 0U) ++s_reenable_calls;
  return repaired;
}

uint32_t Demo_RuntimeStatsTimerRead(void)
{
  return TIM2->CNT;
}
uint32_t Demo_RuntimeStatsTimerInitCount(void)
{
  return s_init_calls;
}
uint32_t Demo_RuntimeStatsTimerReenableCount(void)
{
  return s_reenable_calls;
}
