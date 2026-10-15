#ifndef __STM32L4xx_IT_H
#define __STM32L4xx_IT_H
#ifdef __cplusplus
extern "C" {
#endif
void NMI_Handler(void);
void HardFault_Handler(void);
void MemManage_Handler(void);
void BusFault_Handler(void);
void UsageFault_Handler(void);
void DebugMon_Handler(void);
void SysTick_Handler(void);
void USART1_IRQHandler(void);
void USART2_IRQHandler(void);
void DMA1_Channel6_IRQHandler(void);
void DMA2_Channel1_IRQHandler(void);
void TIM6_DAC_IRQHandler(void);
#ifdef __cplusplus
}
#endif
#endif
