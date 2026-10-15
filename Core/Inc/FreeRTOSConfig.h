#ifndef FREERTOS_CONFIG_H
#define FREERTOS_CONFIG_H

#if defined(__ICCARM__) || defined(__CC_ARM) || defined(__GNUC__)
#include <stdint.h>
extern uint32_t SystemCoreClock;
#endif

#ifndef CMSIS_device_header
#define CMSIS_device_header "stm32l4xx.h"
#endif

#define configENABLE_FPU                         1
#define configENABLE_MPU                         0
#define configUSE_PREEMPTION                     1
#define configSUPPORT_STATIC_ALLOCATION          0
#define configSUPPORT_DYNAMIC_ALLOCATION         1
#define configUSE_IDLE_HOOK                      0
#define configUSE_TICK_HOOK                      0
#define configCPU_CLOCK_HZ                       ( SystemCoreClock )
#define configTICK_RATE_HZ                       ((TickType_t)1000)
#define configMAX_PRIORITIES                     (56)
#define configMINIMAL_STACK_SIZE                 ((uint16_t)128)
#define configTOTAL_HEAP_SIZE                    ((size_t)32768)
#define configMAX_TASK_NAME_LEN                  (16)
#define configUSE_TRACE_FACILITY                 1
/* Real context-switch profiling.  Both hooks execute inside FreeRTOS tasks.c
 * with pxCurrentTCB available.  TIM2 remains the underlying 1 MHz clock.
 * These are lightweight, nonblocking instrumentation functions. */
extern void Demo_TraceTaskSwitchedIn(void *task);
extern void Demo_TraceTaskSwitchedOut(void *task);
#define traceTASK_SWITCHED_IN()  Demo_TraceTaskSwitchedIn((void *)pxCurrentTCB)
#define traceTASK_SWITCHED_OUT() Demo_TraceTaskSwitchedOut((void *)pxCurrentTCB)

#define configGENERATE_RUN_TIME_STATS            1
#define configUSE_STATS_FORMATTING_FUNCTIONS     1
/* 1 MHz TIM2 independent measurement of kernel-scheduled task time. */
extern void Demo_RuntimeStatsTimerInit(void);
extern uint32_t Demo_RuntimeStatsTimerRead(void);
#define portCONFIGURE_TIMER_FOR_RUN_TIME_STATS() Demo_RuntimeStatsTimerInit()
#define portGET_RUN_TIME_COUNTER_VALUE()         Demo_RuntimeStatsTimerRead()
#define configUSE_16_BIT_TICKS                   0
#define configUSE_MUTEXES                        1
#define configQUEUE_REGISTRY_SIZE                8
#define configUSE_RECURSIVE_MUTEXES              1
#define configUSE_COUNTING_SEMAPHORES            1
#define configUSE_PORT_OPTIMISED_TASK_SELECTION  0
#define configUSE_TICKLESS_IDLE                  0
#define configMESSAGE_BUFFER_LENGTH_TYPE         size_t
#define configUSE_CO_ROUTINES                    0
#define configMAX_CO_ROUTINE_PRIORITIES          (2)
#define configUSE_TIMERS                         1
#define configTIMER_TASK_PRIORITY                (2)
#define configTIMER_QUEUE_LENGTH                 10
#define configTIMER_TASK_STACK_DEPTH             256
#define configUSE_NEWLIB_REENTRANT               1
#define configCHECK_FOR_STACK_OVERFLOW           2
#define configUSE_MALLOC_FAILED_HOOK             1

#define configUSE_OS2_THREAD_SUSPEND_RESUME      1
#define configUSE_OS2_THREAD_ENUMERATE           1
#define configUSE_OS2_EVENTFLAGS_FROM_ISR        1
#define configUSE_OS2_THREAD_FLAGS               1
#define configUSE_OS2_TIMER                      1
#define configUSE_OS2_MUTEX                      1

#define INCLUDE_vTaskPrioritySet                 1
#define INCLUDE_uxTaskPriorityGet                1
#define INCLUDE_vTaskDelete                      1
#define INCLUDE_vTaskSuspend                     1
#define INCLUDE_vTaskDelayUntil                  1
#define INCLUDE_vTaskDelay                       1
#define INCLUDE_xTaskGetSchedulerState           1
#define INCLUDE_xTimerPendFunctionCall           1
#define INCLUDE_xQueueGetMutexHolder              1
#define INCLUDE_uxTaskGetStackHighWaterMark      1
#define INCLUDE_xTaskGetCurrentTaskHandle        1
#define INCLUDE_eTaskGetState                    1

#define USE_FreeRTOS_HEAP_4

#ifdef __NVIC_PRIO_BITS
#define configPRIO_BITS __NVIC_PRIO_BITS
#else
#define configPRIO_BITS 4
#endif
#define configLIBRARY_LOWEST_INTERRUPT_PRIORITY         15
#define configLIBRARY_MAX_SYSCALL_INTERRUPT_PRIORITY    5
#define configKERNEL_INTERRUPT_PRIORITY ( configLIBRARY_LOWEST_INTERRUPT_PRIORITY << (8 - configPRIO_BITS) )
#define configMAX_SYSCALL_INTERRUPT_PRIORITY ( configLIBRARY_MAX_SYSCALL_INTERRUPT_PRIORITY << (8 - configPRIO_BITS) )

#define vPortSVCHandler    SVC_Handler
#define xPortPendSVHandler PendSV_Handler

extern void Demo_FreeRTOSAssert(const char *file, uint32_t line);
#define configASSERT(x) do { if ((x) == 0) { Demo_FreeRTOSAssert(__FILE__, (uint32_t)__LINE__); } } while (0)

#endif
