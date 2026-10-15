/*
 * CubeMX FreeRTOS compatibility source.
 *
 * The production firmware uses ONE scheduler startup: main.c calls
 * Demo_StartNativeRTOS() (rtos_bootstrap.c). ControlTask then creates
 * AudioTask, BtTask and BackgroundTask in demo_app.c using xTaskCreate().
 *
 * The CubeMX IOC documents the task arrangement for inspection in the IDE,
 * but MX_FREERTOS_Init() must not create another set of tasks. It is NOT
 * called in the production boot path. The empty compatibility entry point
 * prevents stale CubeMX USER CODE referencing a removed defaultTaskHandle
 * from breaking every subsequent build after task-table changes.
 *
 * If CubeMX Generate Code overwrites this file, run PREPARE_PROJECT.bat
 * again BEFORE compiling. The recovery source and automated checks restore
 * this tested no-op implementation.
 */

void MX_FREERTOS_Init(void)
{
  /* Deliberately unused: native RTOS bootstrap is the only task creator. */
}
