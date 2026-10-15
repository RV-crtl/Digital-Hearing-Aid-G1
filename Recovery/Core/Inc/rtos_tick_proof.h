#ifndef RTOS_TICK_PROOF_H
#define RTOS_TICK_PROOF_H
#include <stdint.h>
/* Low-overhead 1 kHz sampling of the task preempted by the STM32 HAL TIM6
 * interrupt.  Independent of CubeMX-generated FreeRTOSConfig.h and the
 * kernel trace macros.  This is sampled CPU residency, NOT cycle-exact time.
 * Use a >=1s interval and verify board-time and sample count advance. */
void Demo_TickProfilerSample(void);
uint32_t Demo_TickProfilerTaskSamples(void *task);
uint32_t Demo_TickProfilerTotalSamples(void);
uint32_t Demo_TickProfilerDroppedSamples(void);
#endif
