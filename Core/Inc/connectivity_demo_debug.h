#ifndef CONNECTIVITY_DEMO_DEBUG_H
#define CONNECTIVITY_DEMO_DEBUG_H

#include <stdint.h>

/* Debugger Expressions -> g_demo_debug (expand fields).
 * These values come from uxTaskGetSystemState() and TIM2 FreeRTOS profiling.
 * Not a second scheduler, generated traces or fake CPU percentages.
 * All percentages are tenths of a percent: 975 means 97.5%.
 */
typedef struct {
  uint32_t signature;                 /* 0x52544F53 == RTOS */
  uint32_t valid;                     /* 1 once two valid profiler snapshots exist */
  uint32_t sample_number;             /* increments every ~500ms, ONLY when running */
  uint32_t board_time_ms;
  uint32_t measurement_interval_us;
  /* R30.2 runtime-profiler provenance; see PROFILE_DIAGNOSTICS.md. */
  uint32_t profile_reason;           /* 0 good, 1 warmup, 2 no kernel data, 3 timer stalled, 4 runtime counters stalled, 5 elapsed window too short, 6 invalid gap */
  uint32_t profile_timer_count;      /* Raw hardware TIM2->CNT, 1MHz */
  uint32_t profile_kernel_total;     /* Raw FreeRTOS uxTaskGetSystemState total */
  uint32_t profile_kernel_delta;
  uint32_t profile_timer_delta;
  uint32_t profile_wall_delta_ms;
  uint32_t profile_source;            /* 0 none, 1 native FreeRTOS, 2 trace switch hooks, 3 TIM6 sampling */
  uint32_t profile_trace_switches;    /* Real kernel task switch events */
  uint32_t profile_trace_overflow;
  uint32_t profile_tick_samples;        /* 1kHz TIM6 real ISR samples */
  uint32_t profile_tick_dropped;        /* should remain 0 for <=12 tasks */    /* Trace table capacity exceeded */
  uint32_t profile_accounted_delta; /* Sum of selected task runtimes across window */
  uint32_t total_cpu_pct_x10;        /* 1000 = 100.0% non-IDLE */
  uint32_t background_cpu_pct_x10;
  uint32_t audio_cpu_pct_x10;
  uint32_t control_cpu_pct_x10;
  uint32_t bluetooth_cpu_pct_x10;
  uint32_t idle_cpu_pct_x10;
  uint32_t load_mode;                /* 0=OFF, 1=light, 2=stress */
  uint32_t scheduler_state;          /* 2=taskSCHEDULER_RUNNING */
  uint32_t task_count;
  uint32_t control_state;            /* FreeRTOS eTaskState */
  uint32_t audio_state;
  uint32_t bt_state;
  uint32_t background_state;
  uint32_t control_free_stack_words;
  uint32_t audio_free_stack_words;
  uint32_t bt_free_stack_words;
  uint32_t background_free_stack_words;
  uint32_t control_loops;
  uint32_t audio_loops;
  uint32_t bt_loops;
  uint32_t background_loops;
  uint32_t background_work_blocks;
  uint32_t bt_dma_callbacks;
  uint32_t tc_dma_callbacks;
  uint32_t speaker_dma_blocks;
  uint32_t speaker_underruns;
  uint32_t fsm_mode;                  /* 1 startup, 2 active, 3 idle, 4 fault */
  uint32_t selected_source;           /* 0 none, 1 TC, 2 BT */
  uint32_t bt_detected;
  uint32_t tc_detected;
  uint32_t real_fsm_switches;
  uint32_t real_fsm_fault;
  uint32_t direct_mode;               /* 0 = production FSM in control */
  /* R31 dual-task demonstration: append-only, preserve old debugger fields. */
  uint32_t load_b_cpu_pct_x10;
  uint32_t load_b_state;
  uint32_t load_b_free_stack_words;
  uint32_t load_b_loops;
  uint32_t load_b_work_blocks;
  uint32_t dual_load_enabled;
  /* R32: priority-7 bounded demo task (append-only for existing expressions). */
  uint32_t high7_cpu_pct_x10;
  uint32_t high7_state;
  uint32_t high7_free_stack_words;
  uint32_t high7_loops;
  uint32_t high7_work_blocks;
  uint32_t high7_enabled;
} ConnectivityDemoDebug_t;

extern volatile ConnectivityDemoDebug_t g_demo_debug;
#endif
