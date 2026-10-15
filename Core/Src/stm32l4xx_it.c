#include "main.h"
#include "stm32l4xx_it.h"
#include "demo_app.h"
#include "sai.h"
#include "usart.h"

extern TIM_HandleTypeDef htim6;

void NMI_Handler(void) { for (;;) { } }
void HardFault_Handler(void) { Demo_SetError(0xE100U); Demo_ConsolePanic("HARDFAULT",0xE100U); for (;;) { } }
void MemManage_Handler(void) { Demo_SetError(0xE101U); Demo_ConsolePanic("MEMMANAGE",0xE101U); for (;;) { } }
void BusFault_Handler(void) { Demo_SetError(0xE102U); Demo_ConsolePanic("BUSFAULT",0xE102U); for (;;) { } }
void UsageFault_Handler(void) { Demo_SetError(0xE103U); Demo_ConsolePanic("USAGEFAULT",0xE103U); for (;;) { } }
void DebugMon_Handler(void) { }

/* No SysTick_Handler here. CMSIS-RTOS2's cmsis_os2.c supplies the guarded
 * SysTick handler and calls xPortSysTickHandler once FreeRTOS is running.
 * The FreeRTOS Cortex-M4F port supplies SVC_Handler and PendSV_Handler. */

void USART1_IRQHandler(void) { HAL_UART_IRQHandler(&huart1); }
void USART2_IRQHandler(void) { HAL_UART_IRQHandler(&huart2); }
void DMA1_Channel6_IRQHandler(void) { HAL_DMA_IRQHandler(&hdma_sai2_a); }
void DMA2_Channel1_IRQHandler(void) { HAL_DMA_IRQHandler(&hdma_sai1_a); }
void TIM6_DAC_IRQHandler(void) { HAL_TIM_IRQHandler(&htim6); }
