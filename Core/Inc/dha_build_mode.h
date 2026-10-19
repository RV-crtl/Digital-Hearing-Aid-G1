#ifndef DHA_BUILD_MODE_H
#define DHA_BUILD_MODE_H
/*
 * ZEIT4232 SYSTEM DEMONSTRATION POLICY
 * Default image = operating hearing aid. No synthetic CPU workloads are
 * CREATED in this image. This differs from merely leaving them inactive.
 * Laboratory timing demonstrations may opt in via project compiler define
 * -DDHA_ENABLE_RTOS_STRESS=1; never use that build for the system demo.
 *
 * This macro gates only non-product tasks and related command handlers.
 * It must not change: audio DMA, source FSM, task/ISR priorities, TIM2 profiler
 * or the portability of the numerical DSP modules.
 */
#ifndef DHA_ENABLE_RTOS_STRESS
#define DHA_ENABLE_RTOS_STRESS 0
#endif
#if (DHA_ENABLE_RTOS_STRESS != 0) && (DHA_ENABLE_RTOS_STRESS != 1)
#error "DHA_ENABLE_RTOS_STRESS must be 0 or 1"
#endif
#endif
