#ifndef RTOS_STATS_TIMER_H
#define RTOS_STATS_TIMER_H
#include <stdint.h>
void Demo_RuntimeStatsTimerInit(void);
uint32_t Demo_RuntimeStatsTimerRead(void);
uint32_t Demo_RuntimeStatsTimerEnsureRunning(void);
uint32_t Demo_RuntimeStatsTimerInitCount(void);
uint32_t Demo_RuntimeStatsTimerReenableCount(void);
#endif
