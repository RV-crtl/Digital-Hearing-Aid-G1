#ifndef RTOS_TRACE_PROOF_H
#define RTOS_TRACE_PROOF_H
#include <stdint.h>
/* FreeRTOS traceTASK_SWITCHED_IN/OUT hooks.  A second measurement channel,
 * independent of uxTaskGetSystemState's currently zero runtime counters.
 * This counts REAL switch events and the actual TIM2 time between them.
 * Never invokes HAL, allocates memory, or blocks from the scheduler. */
void Demo_TraceTaskSwitchedIn(void *task);
void Demo_TraceTaskSwitchedOut(void *task);
uint32_t Demo_TraceGetTaskTime(void *task);
uint32_t Demo_TraceGetSwitches(void);
uint32_t Demo_TraceGetOverflow(void);
#endif
