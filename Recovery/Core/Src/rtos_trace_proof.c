#include "rtos_trace_proof.h"
#ifndef RTOS_TRACE_HOST_TEST
#include "stm32l4xx.h"
#define DEMO_TRACE_CLOCK_READY() (((RCC->APB1ENR1 & RCC_APB1ENR1_TIM2EN) != 0U) && ((TIM2->CR1 & TIM_CR1_CEN) != 0U))
#define DEMO_TRACE_CLOCK() (TIM2->CNT)
#else
extern uint32_t Demo_TraceTestTime(void);
#define DEMO_TRACE_CLOCK_READY() (1)
#define DEMO_TRACE_CLOCK() Demo_TraceTestTime()
#endif
#define TRACE_TASK_CAPACITY 12U
typedef struct {
  void *task;
  volatile uint32_t ticks;
} TraceRecord;
static TraceRecord g_records[TRACE_TASK_CAPACITY];
static void *volatile g_active_task;
static uint32_t g_started_at;
static volatile uint32_t g_switches;
static volatile uint32_t g_overflow;

static TraceRecord *trace_lookup(void *task, uint32_t create)
{
  TraceRecord *free_slot=0;
  if (task == 0) return 0;
  for (uint32_t i=0; i<TRACE_TASK_CAPACITY; ++i) {
    if (g_records[i].task==task) return &g_records[i];
    if (g_records[i].task==0 && free_slot==0) free_slot=&g_records[i];
  }
  if (create && free_slot) { free_slot->task=task; return free_slot; }
  if (create) ++g_overflow;
  return 0;
}

void Demo_TraceTaskSwitchedIn(void *task)
{
  if (!DEMO_TRACE_CLOCK_READY()) return;
  (void)trace_lookup(task,1U);
  g_started_at=DEMO_TRACE_CLOCK();
  g_active_task=task;
}

void Demo_TraceTaskSwitchedOut(void *task)
{
  if (!DEMO_TRACE_CLOCK_READY() || !task || task!=g_active_task) return;
  uint32_t now=DEMO_TRACE_CLOCK();
  TraceRecord *r=trace_lookup(task,0U);
  if (r) r->ticks += now-g_started_at;  /* uint32 wrap-safe interval */
  ++g_switches;
  g_active_task=0;
}

uint32_t Demo_TraceGetTaskTime(void *task)
{
  TraceRecord *r=trace_lookup(task,0U);
  return r ? r->ticks : 0U;
}
uint32_t Demo_TraceGetSwitches(void) {return g_switches;}
uint32_t Demo_TraceGetOverflow(void) {return g_overflow;}
