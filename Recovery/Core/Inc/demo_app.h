#ifndef DEMO_APP_H
#define DEMO_APP_H

#include "main.h"
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define DEMO_MAGIC                0x44484152UL /* 'DHAR' */
#define DEMO_FW_VERSION           0x00041800UL
#define DEMO_AUDIO_RATE_HZ        48000U
#define DEMO_SAI_KERNEL_HZ        24571428U
#define DEMO_DMA_WORDS            512U
#define DEMO_HALF_WORDS           (DEMO_DMA_WORDS / 2U)
#define DEMO_FRAMES_PER_HALF      (DEMO_HALF_WORDS / 2U)
#define DEMO_OUTPUT_DMA_SAMPLES    (DEMO_FRAMES_PER_HALF * 4U) /* stereo, two half-buffers */
#define DEMO_OUTPUT_HALF_SAMPLES   (DEMO_OUTPUT_DMA_SAMPLES / 2U)
#define DEMO_OUTPUT_QUEUE_DEPTH    16U
#define DEMO_PLOT_SAMPLES          256U
#define DEMO_PLOT_RATE_HZ          12000U
#define DEMO_RAW_CAPTURE_FRAMES     4096U
#define DEMO_RAW_CAPTURE_WORDS      (DEMO_RAW_CAPTURE_FRAMES * 2U)
#define DEMO_LISTEN_CAPTURE_SAMPLES 16384U /* 341.3 ms of pre-DSP mono at 48 kHz in SRAM2 */

/* requested_source_mode */
#define DEMO_MODE_AUTO            0U
#define DEMO_MODE_BT_ONLY         1U
#define DEMO_MODE_TC_ONLY         2U
#define DEMO_MODE_OFF             3U

/* bt_link_mode */
#define DEMO_BTLINK_AUTO          0U
#define DEMO_BTLINK_FORCE_OFF     1U
#define DEMO_BTLINK_FORCE_ON      2U


/* transparent/direct reference path */
#define DEMO_DIRECT_OFF            0U
#define DEMO_DIRECT_BT             1U /* current SAI1A-labelled input */
#define DEMO_DIRECT_TC             2U /* current SAI2A-labelled input */

#define DEMO_CHANNEL_MIX           0U
#define DEMO_CHANNEL_LEFT          1U
#define DEMO_CHANNEL_RIGHT         2U

/* Bluetooth stereo-to-mono diagnostic modes. */
#define DEMO_BT_MONO_MIX          0U
#define DEMO_BT_MONO_LEFT         1U
#define DEMO_BT_MONO_RIGHT        2U
#define DEMO_BT_MONO_AUTO         3U

typedef struct {
  volatile uint32_t requested_source_mode;
  volatile uint32_t bt_link_mode;
  volatile uint32_t interfaces_ready;
  volatile uint32_t background_load_enabled;
  volatile uint32_t stream_enabled;
  volatile uint32_t audio_capture_enabled;
  volatile uint32_t output_enabled;
  volatile uint32_t output_gain_q15;
  volatile uint32_t dsp_enabled;
  volatile uint32_t evidence_enabled;
  volatile uint32_t plot_enabled;
  volatile uint32_t bt_mono_mode;
  volatile uint32_t pcm_decode_mode; /* 0=low24, 1=upper16, 2=middle16, 3=low16 */
  volatile uint32_t direct_source;   /* 0=off, 1=BT/SAI1A, 2=TC/SAI2A */
  volatile uint32_t direct_channel;  /* 0=mix, 1=left, 2=right */
  volatile uint32_t test_tone_step; /* 0=off, 1=1k, 2=2k, 3=3k */
  volatile uint32_t apply_bt_profile_request;
  volatile uint32_t bt_query_request;
  volatile uint32_t reset_diagnostics_request;
  volatile uint32_t bt_present_rms_threshold;
  volatile uint32_t bt_present_pp_threshold;
} DemoControl_t;

typedef struct {
  volatile uint32_t magic;
  volatile uint32_t fw_version;
  volatile uint32_t stage;
  volatile uint32_t error_code;
  volatile uint32_t uptime_ms;

  volatile uint32_t sysclk_hz;
  volatile uint32_t hclk_hz;
  volatile uint32_t pclk1_hz;
  volatile uint32_t pclk2_hz;
  volatile uint32_t usart1_clk_hz;
  volatile uint32_t usart2_clk_hz;
  volatile uint32_t sai1_clk_hz;
  volatile uint32_t sai2_clk_hz;
  volatile uint32_t clock_warning_flags;

  volatile uint32_t console_rx_bytes;
  volatile uint32_t console_tx_bytes;
  volatile uint32_t console_lines;
  volatile uint32_t console_commands_ok;
  volatile uint32_t console_commands_error;
  volatile uint32_t console_rx_overruns;
  volatile uint32_t console_uart_errors;
  volatile uint32_t console_rx_irq_armed;

  volatile uint32_t control_task_loops;
  volatile uint32_t audio_task_loops;
  volatile uint32_t bt_task_loops;
  volatile uint32_t background_task_loops;
  volatile uint32_t background_work_iterations;
  volatile uint32_t control_stack_high_water;
  volatile uint32_t audio_stack_high_water;
  volatile uint32_t bt_stack_high_water;
  volatile uint32_t background_stack_high_water;

  volatile uint32_t bt_half_callbacks;
  volatile uint32_t bt_full_callbacks;
  volatile uint32_t tc_half_callbacks;
  volatile uint32_t tc_full_callbacks;
  volatile uint32_t bt_dma_overruns;
  volatile uint32_t tc_dma_overruns;
  volatile uint32_t sai1_errors;
  volatile uint32_t sai2_errors;
  volatile uint32_t last_sai1_error;
  volatile uint32_t last_sai2_error;
  volatile uint32_t audio_dma_running;

  volatile uint32_t bt_uart_rx_bytes;
  volatile uint32_t bt_uart_tx_bytes;
  volatile uint32_t bt_uart_lines;
  volatile uint32_t bt_uart_overruns;
  volatile uint32_t bt_uart_errors;
  volatile uint32_t bt_rx_irq_armed;
  volatile uint32_t bt_profile_attempts;
  volatile uint32_t bt_profile_successes;
  volatile uint32_t bt_profile_commands_sent;

  volatile uint32_t bt_blocks_processed;
  volatile uint32_t tc_blocks_processed;
  volatile uint32_t bt_rms;
  volatile uint32_t bt_peak_to_peak;
  volatile uint32_t bt_left_rms;
  volatile uint32_t bt_right_rms;
  volatile uint32_t bt_mono_selected;
  volatile uint32_t bt_capture_depth;
  volatile uint32_t tc_capture_depth;
  volatile uint32_t bt_capture_max_depth;
  volatile uint32_t tc_capture_max_depth;
  volatile uint32_t bt_audio_present;
  volatile uint32_t tc_rms;
  volatile uint32_t tc_peak_to_peak;
  volatile uint32_t tc_valid;
  volatile uint32_t processed_output_peak;

  volatile uint32_t speaker_dma_running;
  volatile uint32_t speaker_half_callbacks;
  volatile uint32_t speaker_full_callbacks;
  volatile uint32_t speaker_blocks_output;
  volatile uint32_t speaker_underruns;
  volatile uint32_t speaker_queue_overruns;
  volatile uint32_t speaker_refill_misses;
  volatile uint32_t speaker_queue_depth;
  volatile uint32_t speaker_queue_max_depth;
  volatile uint32_t test_tone_hz;

  volatile uint32_t dsp_blocks;
  volatile uint32_t dsp_input_rms;
  volatile uint32_t dsp_output_rms;
  volatile uint32_t dsp_input_peak;
  volatile uint32_t dsp_output_peak;
  volatile int32_t  dsp_gain_q15;
  volatile uint32_t dsp_last_us;
  volatile uint32_t dsp_max_us;
  volatile uint32_t dsp_limited_samples;

  volatile uint32_t control_task_rate_hz;
  volatile uint32_t audio_task_rate_hz;
  volatile uint32_t bt_task_rate_hz;
  volatile uint32_t background_task_rate_hz;
  volatile uint32_t evidence_reports;
  volatile uint32_t plot_frames_sent;

  volatile uint32_t mode;
  volatile uint32_t active_source;
  volatile uint32_t fault;
  volatile uint32_t muted;
  volatile uint32_t source_switches;
  volatile uint32_t faults_entered;
  volatile uint32_t recoveries;
  volatile uint32_t controller_updates;
  volatile uint32_t service_deadline_misses;
  volatile uint32_t max_service_interval_ms;

  volatile uint32_t selftest_runs;
  volatile uint32_t selftest_failures;
  volatile uint32_t last_selftest_mask;

  volatile uint32_t raw_capture_source;
  volatile uint32_t raw_capture_frames;
  volatile uint32_t raw_capture_ready;
  volatile uint32_t raw_capture_dumps;
  volatile uint32_t listen_capture_source;
  volatile uint32_t listen_capture_channel;
  volatile uint32_t listen_capture_samples;
  volatile uint32_t listen_capture_ready;
  volatile uint32_t listen_capture_dumps;
} DemoDiagnostics_t;

extern volatile DemoControl_t g_demo_control;
extern volatile DemoDiagnostics_t g_demo_diag;
extern uint32_t g_bt_dma_words[DEMO_DMA_WORDS];
extern uint32_t g_tc_dma_words[DEMO_DMA_WORDS];
extern int16_t g_demo_processed_mono[DEMO_FRAMES_PER_HALF];
extern int16_t g_speaker_tx_dma[DEMO_OUTPUT_DMA_SAMPLES];

void Demo_EarlyInit(void);
void Demo_AppInit(void);
void Demo_SetStage(uint32_t stage);
void Demo_SetError(uint32_t error_code);
void Demo_ConsoleEarly(const char *message);
void Demo_ConsoleRaw(const char *message);
void Demo_ConsolePanic(const char *tag, uint32_t code);
void Demo_ControlTask(void *argument);
void Demo_FreeRTOSAssert(const char *file, uint32_t line);

#ifdef __cplusplus
}
#endif

#endif
