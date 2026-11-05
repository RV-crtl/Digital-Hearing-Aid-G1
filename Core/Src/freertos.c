/*
 * CubeMX FreeRTOS compatibility source.
 *
 * The production firmware uses ONE scheduler startup: main.c calls
 * Demo_StartNativeRTOS() (rtos_bootstrap.c). ControlTask then creates
 * AudioTask and BtTask in demo_app.c using xTaskCreate(). Lab-only stress
 * workers are conditional and never created in the operating firmware.
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

#include "FreeRTOS.h"

/* FreeRTOS heap_4.c refers to this exact global array when
 * configAPPLICATION_ALLOCATED_HEAP == 1. CPU-only RTOS allocations (tasks,
 * queues, stacks) are placed in SRAM2; peripheral DMA data remains in SRAM1.
 * The Reset_Handler clears this region to zero on every power-on/reset.
 * This file is restored from Recovery after CubeMX generation.
 */
__attribute__((section(".freertos_heap_ram2"), aligned(8), used))
uint8_t ucHeap[configTOTAL_HEAP_SIZE];

void MX_FREERTOS_Init(void)
{
  /* Deliberately unused: native RTOS bootstrap is the only task creator. */
}
