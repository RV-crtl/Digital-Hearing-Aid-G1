#include "rtos_tick_proof.h"
#ifndef RTOS_TICK_HOST_TEST
#include "FreeRTOS.h"
#include "task.h"
#else
extern int Demo_TickTestSchedulerRunning(void);
extern void *Demo_TickTestTask(void);
#endif
/* Interrupt-only writes, task-side reads.  Each slot is naturally aligned,
 * 32-bit accesses are atomic on the Cortex-M4.  No heap, HAL or RTOS mutating
 * operation is performed inside the interrupt. */
#define PROOF_TICK_CAPACITY 12U
typedef struct { void *task; volatile uint32_t samples; } ProofTickSlot;
static ProofTickSlot s_slots[PROOF_TICK_CAPACITY];
static volatile uint32_t s_total;
static volatile uint32_t s_dropped;

void Demo_TickProfilerSample(void)
{
#ifndef RTOS_TICK_HOST_TEST
  if (xTaskGetSchedulerState() != taskSCHEDULER_RUNNING) return;
  void *task = (void *)xTaskGetCurrentTaskHandle();
#else
  if (!Demo_TickTestSchedulerRunning()) return;
  void *task = Demo_TickTestTask();
#endif
  if (!task) return;
  for (uint32_t i=0U; i<PROOF_TICK_CAPACITY; i++) {
    if (s_slots[i].task == task) {
      s_slots[i].samples++;
      s_total++;
      return;
    }
  }
  for (uint32_t i=0U; i<PROOF_TICK_CAPACITY; i++) {
    if (s_slots[i].task == (void *)0) {
      s_slots[i].task = task;
      s_slots[i].samples = 1U;
      s_total++;
      return;
    }
  }
  s_total++;
  s_dropped++;
}

uint32_t Demo_TickProfilerTaskSamples(void *task)
{
  if (!task) return 0U;
  for (uint32_t i=0U; i<PROOF_TICK_CAPACITY; i++) {
    if (s_slots[i].task == task) return s_slots[i].samples;
  }
  return 0U;
}
uint32_t Demo_TickProfilerTotalSamples(void) { return s_total; }
uint32_t Demo_TickProfilerDroppedSamples(void) { return s_dropped; }
