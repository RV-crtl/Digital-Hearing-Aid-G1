#include "rtos_bootstrap.h"
#include "demo_app.h"
#include "FreeRTOS.h"
#include "task.h"

#include <stdint.h>

static TaskHandle_t g_bootstrap_task = NULL;

static void BootstrapTask(void *argument)
{
  (void)argument;

  /* This is deliberately a raw, polling USART2 write. It does not depend on
   * the HAL tick, printf/newlib, mutexes or CMSIS-RTOS2. If this appears, the
   * Cortex-M4 FreeRTOS port has successfully restored the first task context. */
  Demo_ConsoleRaw("\r\nRTOS D: FIRST TASK ENTERED - scheduler handoff is alive\r\n");
  Demo_SetStage(0x48U);

  Demo_ConsoleRaw("RTOS E: initialising production PD5 application\r\n");
  Demo_AppInit();
  if (g_demo_diag.error_code != 0U) {
    Demo_ConsolePanic("APP_INIT", g_demo_diag.error_code);
    taskDISABLE_INTERRUPTS();
    for (;;) { }
  }

  Demo_ConsoleRaw("RTOS F: PD5 init complete; entering ControlTask\r\n");

  /* Demo_ControlTask becomes the long-lived control/UI task. It creates the
   * Audio, Bluetooth and Background tasks only after the scheduler is proven. */
  Demo_ControlTask(NULL);

  Demo_ConsolePanic("CONTROL_RETURN", 0xF006U);
  vTaskDelete(NULL);
}

void Demo_StartNativeRTOS(void)
{
  BaseType_t rc;

  Demo_SetStage(0x40U);
  Demo_ConsoleRaw("RTOS A: native FreeRTOS bootstrap (CMSIS osKernelStart bypassed)\r\n");
  Demo_ConsoleRaw("RTOS B: creating one bootstrap task\r\n");

  rc = xTaskCreate(BootstrapTask,
                   "ControlTask",
                   1024U,
                   NULL,
                   3U,
                   &g_bootstrap_task);

  if (rc != pdPASS || g_bootstrap_task == NULL) {
    Demo_SetError(0xF004U);
    Demo_ConsolePanic("BOOT_TASK_CREATE", 0xF004U);
    taskDISABLE_INTERRUPTS();
    for (;;) { }
  }

  Demo_ConsoleRaw("RTOS C: bootstrap created; calling vTaskStartScheduler now\r\n");

  /* Native FreeRTOS start. This avoids the CMSIS-RTOS2 kernel-state wrapper,
   * which was the only code between the last known-good boot print and the
   * previous silent stop. */
  vTaskStartScheduler();

  /* vTaskStartScheduler only returns if the port cannot start (normally idle
   * or timer task allocation failure). */
  Demo_SetError(0xF005U);
  Demo_ConsolePanic("SCHED_RETURN", 0xF005U);
  taskDISABLE_INTERRUPTS();
  for (;;) { }
}
