#ifndef RTOS_BOOTSTRAP_H
#define RTOS_BOOTSTRAP_H

#ifdef __cplusplus
extern "C" {
#endif

/* Creates one FreeRTOS bootstrap/control task and starts the native FreeRTOS
 * scheduler. This function does not return on a healthy system. */
void Demo_StartNativeRTOS(void);

#ifdef __cplusplus
}
#endif

#endif
