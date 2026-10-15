#include "demo_app.h"
#include "sai.h"
#include "speaker_output.h"
#include "usart.h"
#include "connectivity_service.h"
#include "control_protocol.h"
#include "pcm_transport.h"
#include "audio_processing.h"
#include "audio_dynamics.h"
#include "bt_profile.h"
#include "tc_cleanup.h"
#include "connectivity_demo_debug.h"
#include "rtos_trace_proof.h"
#include "rtos_tick_proof.h"
#include "rtos_stats_timer.h"
#include "group_features.h"
#include "group_guardian.h"
#include "queue.h"
#include "FreeRTOS.h"
#include "task.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

volatile DemoControl_t g_demo_control;
volatile DemoDiagnostics_t g_demo_diag;
/* Deliberately externally visible for STM32CubeIDE Expressions debugger. */
__attribute__((used)) volatile ConnectivityDemoDebug_t g_demo_debug;
uint32_t g_bt_dma_words[DEMO_DMA_WORDS];
uint32_t g_tc_dma_words[DEMO_DMA_WORDS];
int16_t g_demo_processed_mono[DEMO_FRAMES_PER_HALF];
int16_t g_speaker_tx_dma[DEMO_OUTPUT_DMA_SAMPLES];

static ConnectivityService_t g_service;
static AudioDynamics_t g_dynamics;
static GroupFeatures g_group_features;
static GroupGuardian g_group_guardian;
static uint8_t g_group_guard_bypass;
/* Event queue: sole author of DSP state is AudioTask; ControlTask/button only enqueue. */
static QueueHandle_t g_group_events;
typedef enum { GROUP_EVENT_ENABLE=1, GROUP_EVENT_PROFILE=2,
               GROUP_EVENT_AUTO=3 } GroupEventType;
typedef struct { uint8_t type, value; } GroupEvent;
/* External debugger-visible telemetry, AudioTask writer. */
typedef struct {
    volatile uint32_t enabled, automatic, profile, scene, scene_updates;
    volatile uint32_t gain_q15, input_mean_abs, processed_blocks, clipped;
    volatile uint32_t event_count, event_drops, button_presses;
    volatile uint32_t guardian_level, guardian_events, guardian_recoveries;
} GroupTelemetry;
__attribute__((used)) volatile GroupTelemetry g_group_telemetry;
static uint32_t g_group_events_seen;
static uint32_t g_group_button_seen;
static uint8_t g_group_dsp_saved;
static uint8_t g_group_old_dsp_on, g_group_old_comp, g_group_old_eq;
static uint8_t g_group_last_applied_profile=0xFFU;

static int16_t g_left[DEMO_FRAMES_PER_HALF];
static int16_t g_right[DEMO_FRAMES_PER_HALF];
static int16_t g_mono[DEMO_FRAMES_PER_HALF];
static int16_t g_routed[DEMO_FRAMES_PER_HALF];
static int16_t g_r27_tc_clean[DEMO_FRAMES_PER_HALF];
static int16_t g_r27_pre_source[DEMO_FRAMES_PER_HALF];
static TcCleanup g_r27_tc_filter;
static volatile uint8_t g_r27_tc_clean_enabled=1U;


/* R9 transport-integrity architecture.
 * RX DMA callbacks snapshot each completed half-buffer immediately, before the
 * circular DMA can overwrite it.  Output is now a MONO SAMPLE FIFO rather than
 * a block FIFO.  SAI2B consumes that FIFO with a very small adaptive fractional
 * rate match, so tiny RX/TX clock differences cannot accumulate until an entire
 * 128-frame block has to be dropped/repeated.  Whole-block discontinuities were
 * especially audible as low-frequency hum/robotic speech. */
#define CAPTURE_RING_DEPTH 8U
#define CAPTURE_RING_MASK  (CAPTURE_RING_DEPTH - 1U)
#define OUTPUT_SAMPLE_FIFO_SAMPLES 4096U
#define OUTPUT_SAMPLE_FIFO_MASK    (OUTPUT_SAMPLE_FIFO_SAMPLES - 1U)
#define OUTPUT_PRIME_SAMPLES_EXACT 1024U   /* 21.3 ms: generous jitter reserve, still exact 1:1 */
#define OUTPUT_PRIME_SAMPLES_ASYNC 2048U   /* 42.7 ms for independently-clocked SAI2A/telecoil */
#define OUTPUT_TARGET_SAMPLES      2048U   /* elastic-controller centre for asynchronous input */
#define OUTPUT_STEP_Q16_ONE        65536UL
#define OUTPUT_STEP_Q16_MAX_CORR   128L    /* +/-0.1953125% hard limit */
#define OUTPUT_DEPTH_LP_SHIFT      6U      /* ~170-ms depth smoothing at 375 half-buffers/s */
#define OUTPUT_FADE_STEP_Q15       512U    /* 64-sample / 1.33-ms click-free fade-in */
#if ((CAPTURE_RING_DEPTH & (CAPTURE_RING_DEPTH - 1U)) != 0U)
#error "CAPTURE_RING_DEPTH must be a power of two"
#endif
#if ((OUTPUT_SAMPLE_FIFO_SAMPLES & (OUTPUT_SAMPLE_FIFO_SAMPLES - 1U)) != 0U)
#error "OUTPUT_SAMPLE_FIFO_SAMPLES must be a power of two"
#endif

typedef struct {
  uint32_t words[DEMO_HALF_WORDS];
} CaptureBlock_t;

static CaptureBlock_t g_bt_capture[CAPTURE_RING_DEPTH];
static CaptureBlock_t g_tc_capture[CAPTURE_RING_DEPTH];
static volatile uint8_t g_bt_cap_head, g_bt_cap_tail;
static volatile uint8_t g_tc_cap_head, g_tc_cap_tail;
static uint32_t g_bt_work_words[DEMO_HALF_WORDS];
static uint32_t g_tc_work_words[DEMO_HALF_WORDS];

/* Single-producer (AudioTask), single-consumer (SAI2B DMA ISR) elastic FIFO.
 * Counts are monotonic; only indexing wraps. */
static int16_t g_output_samples[OUTPUT_SAMPLE_FIFO_SAMPLES];
static volatile uint32_t g_output_write_count;
static volatile uint32_t g_output_read_count;
static volatile uint32_t g_output_phase_q16;
static volatile uint32_t g_output_step_q16;
static volatile uint32_t g_output_depth_lp_q8;
static volatile uint32_t g_output_fade_q15;
static volatile uint32_t g_output_depth_min_samples;
static volatile uint32_t g_output_depth_max_samples;
static volatile uint8_t g_output_playback_primed;
static volatile uint8_t g_tone_phase;
static uint32_t g_last_output_source;

static const int16_t k_sine_48[48] = {
       0,  4277,  8481, 12539, 16383, 19947, 23170, 25996,
   28377, 30273, 31650, 32487, 32767, 32487, 31650, 30273,
   28377, 25996, 23170, 19947, 16383, 12539,  8481,  4277,
       0, -4277, -8481,-12539,-16383,-19947,-23170,-25996,
  -28377,-30273,-31650,-32487,-32767,-32487,-31650,-30273,
  -28377,-25996,-23170,-19947,-16384,-12539, -8481, -4277
};

/* R21 recovery layer.  The R9 transport remains intact; these flags only
 * control transparent mono selection, level recovery and short dropout
 * concealment.  No equaliser/compressor/filter is inserted in the reference
 * Bluetooth path. */
static volatile uint8_t g_r21_auto_level_enabled;
static volatile uint8_t g_r21_smart_mono_enabled;
static uint32_t g_r21_auto_gain_q15 = 32768U;
static int16_t g_r21_last_good_output[DEMO_OUTPUT_HALF_SAMPLES];
static volatile uint8_t g_r21_have_last_good_output;
static volatile uint8_t g_r21_conceal_streak;
static volatile uint32_t g_r21_conceal_total;
static volatile uint32_t g_r21_tx_failures;
static uint8_t g_r24_gain_update_div;
/* R26: smoothed stereo fold avoids block-to-block LEFT/MIX/RIGHT switching. */
static uint32_t g_r26_left_weight_q15 = 16384U;
static uint32_t g_r26_target_left_weight_q15 = 16384U;
static char g_r21_line[1280];

#define PLOT_ACCUM_SAMPLES (DEMO_PLOT_SAMPLES * 4U)
static int16_t g_plot_raw_accum[PLOT_ACCUM_SAMPLES];
static int16_t g_plot_proc_accum[PLOT_ACCUM_SAMPLES];
static uint16_t g_plot_accum_count;
static int16_t g_plot_raw_snapshot[DEMO_PLOT_SAMPLES];
static int16_t g_plot_proc_snapshot[DEMO_PLOT_SAMPLES];
static volatile uint32_t g_plot_snapshot_seq;
static uint32_t g_plot_last_sent_seq;
static uint32_t g_last_plot_send_ms;
#define R262_HD_SAMPLES 2048U
static int16_t g_r262_hd_buffer[R262_HD_SAMPLES];
static volatile uint16_t g_r262_hd_valid;
static volatile uint32_t g_r262_hd_seq;
static uint32_t g_r262_hd_source;

/* R6 raw-input recorder. This buffer stores the exact 32-bit SAI words that
 * reached the application before PCM alignment, mono folding, routing or DSP.
 * 4096 stereo frames = 32768 bytes, giving 11.7-Hz FFT bin spacing at 48 kHz. */
#define RAW_CAPTURE_SOURCE_NONE 0U
#define RAW_CAPTURE_SOURCE_BT   1U
#define RAW_CAPTURE_SOURCE_TC   2U
static uint32_t *const g_raw_capture_words = (uint32_t *)SRAM2_BASE;
static volatile uint32_t g_raw_capture_source;
static volatile uint32_t g_raw_capture_frames;
static volatile uint8_t g_raw_capture_armed;
static volatile uint8_t g_raw_capture_ready;
static uint8_t g_raw_capture_announced;

/* R9 listen recorder. Uses the same SRAM2 bank as raw capture, but stores
 * decoded PRE-DSP mono int16 PCM. 16384 samples = 341.3 ms at 48 kHz: long
 * enough to hear tone quality and short speech syllables on the PC. Raw and
 * listen capture are mutually exclusive by construction. */
static int16_t *const g_listen_capture_pcm = (int16_t *)SRAM2_BASE;
static volatile uint32_t g_listen_capture_source;
static volatile uint32_t g_listen_capture_channel;
static volatile uint32_t g_listen_capture_samples;
static volatile uint8_t g_listen_capture_armed;
static volatile uint8_t g_listen_capture_ready;
static uint8_t g_listen_capture_announced;

static uint32_t g_last_evidence_ms;
static uint32_t g_last_evidence_report_ms;
static uint32_t g_last_rate_ctl;
static uint32_t g_last_rate_aud;
static uint32_t g_last_rate_bt;
static uint32_t g_last_rate_bg;

/* Capture rings replace the old event-ID queue.  An event without a snapshot
 * was unsafe because the referenced circular-DMA half could be overwritten
 * before AudioTask reached it. */
static volatile uint32_t g_pending_command_plus1;
/* A real-demo reset is requested by ControlTask but executed only by
 * AudioTask, the sole owner of ConnectivityService and its FSM. */
static volatile uint8_t g_r28_route_restart_pending;
static volatile uint32_t g_r28_route_epoch;
/* Real Bluetooth ADC quiet-level qualification for Stage 6 only.
 * State 0 idle, 1 capturing BT silence, 2 done, 3 rejected noisy baseline. */
static volatile uint8_t g_r28_cal_state;
static volatile uint32_t g_r28_cal_blocks, g_r28_cal_max_rms, g_r28_cal_max_pp;


static TaskHandle_t g_audio_task;
static TaskHandle_t g_bt_task;
static TaskHandle_t g_background_task;
static TaskHandle_t g_cpu_load_b_task;
static TaskHandle_t g_r32_hi_task;
static uint8_t g_children_started;
static uint8_t g_dma_started;
static uint8_t g_bt_dma_started;
static uint8_t g_tc_dma_started;
static uint8_t g_speaker_dma_started;
static uint8_t g_bt_present_streak;
static uint8_t g_bt_absent_streak;
static uint32_t g_last_stream_ms;
static uint32_t g_last_state_step_ms;
static uint32_t g_last_requested_mode = 0xFFFFFFFFUL;

#define CONSOLE_LINE_LEN 96U
#define BT_LINE_LEN 96U
#define UART_RX_RING_SIZE 256U
#if ((UART_RX_RING_SIZE & (UART_RX_RING_SIZE - 1U)) != 0U)
#error "UART_RX_RING_SIZE must be a power of two"
#endif
static char g_console_line[CONSOLE_LINE_LEN];
static uint32_t g_console_line_len;
static char g_bt_line[BT_LINE_LEN];
static uint32_t g_bt_line_len;

/* Interrupt-driven RX rings.  The previous R2 build polled HAL_UART_Receive()
 * every 5/10 ms.  At 115200 baud a complete command arrives in far less than
 * 5 ms, while the STM32 UART only has a one-byte receive data register.  That
 * caused ORE/byte loss and made commands such as STREAM OFF appear dead. */
static volatile uint8_t g_console_rx_ring[UART_RX_RING_SIZE];
static volatile uint16_t g_console_rx_head;
static volatile uint16_t g_console_rx_tail;
static uint8_t g_console_rx_irq_byte;

static volatile uint8_t g_bt_rx_ring[UART_RX_RING_SIZE];
static volatile uint16_t g_bt_rx_head;
static volatile uint16_t g_bt_rx_tail;
static uint8_t g_bt_rx_irq_byte;

static void r28_real_route_arm(void);
static void r28_real_fsm_observe(void);
#define DEMO_STRESS_DURATION_MS 60000U
static volatile uint32_t g_stress_stop_at_ms;
static volatile uint32_t g_stress_checksum;
/* R31: visible second CPU-bound task; independent work and progress counters.
 * Both background workers have priority 2. Do not change the profiler. */
__attribute__((used)) volatile uint32_t g_r31_cpu_b_work_blocks;
__attribute__((used)) volatile uint32_t g_r31_cpu_b_loops;
__attribute__((used)) volatile uint32_t g_r31_cpu_b_checksum;
static volatile uint8_t g_r31_dual_load;
/* R33: higher-demand bounded PRIORITY 7 experiment. Independent of R31 priority-2 modes.
 * This flag and timer are read by the worker; the independent task timeout
 * prevents a stuck console from leaving the audio deadline stressed forever. */
static volatile uint8_t g_r32_hi_active;
#define R34_SPIN_DEADLINE_MS 20000U
static volatile uint32_t g_r32_hi_deadline_ms;
__attribute__((used)) volatile uint32_t g_r32_hi_work_blocks;
__attribute__((used)) volatile uint32_t g_r32_hi_loops;
__attribute__((used)) volatile uint32_t g_r32_hi_checksum;

static void r28_rtos_snapshot(void);
static void r29_refresh_debug_snapshot(uint32_t ms);
static void StartAudioTask(void *argument);
static void StartBtTask(void *argument);
static void StartBackgroundTask(void *argument);
static void StartCpuLoadB(void *argument);
static void StartHighPriorityLoad(void *argument);
static const char *source_name(uint32_t s);
static void metrics_s16(const int16_t *x, size_t n, uint32_t *rms, uint32_t *pp, uint32_t *peak);
static void console_write(const char *s);
static HAL_StatusTypeDef uart2_send_binary_chunked(const uint8_t *data, uint32_t len);
static void console_u32(uint32_t v);
static void put_u16_le(uint8_t *p, uint16_t v);
static void put_u32_le(uint8_t *p, uint32_t v);
static void raw_capture_arm(uint32_t source);
static void raw_capture_append(uint32_t source, const uint32_t *words, uint32_t frame_count);
static void raw_capture_send(void);
static void listen_capture_arm(uint32_t source, uint32_t channel);
static void listen_capture_append(uint32_t source, const int16_t *left, const int16_t *right, uint32_t frame_count);
static void listen_capture_send(void);
static void direct_process_block(uint32_t source, const int16_t *left, const int16_t *right, uint32_t lrms, uint32_t rrms);

static const char *raw_capture_source_name(uint32_t source)
{
  if (source == RAW_CAPTURE_SOURCE_BT) return "BT";
  if (source == RAW_CAPTURE_SOURCE_TC) return "TC";
  return "NONE";
}

static void raw_capture_arm(uint32_t source)
{
  taskENTER_CRITICAL();
  g_listen_capture_armed = 0U;
  g_listen_capture_ready = 0U;
  g_listen_capture_samples = 0U;
  g_raw_capture_source = source;
  g_raw_capture_frames = 0U;
  g_raw_capture_armed = (source == RAW_CAPTURE_SOURCE_BT || source == RAW_CAPTURE_SOURCE_TC) ? 1U : 0U;
  g_raw_capture_ready = 0U;
  g_raw_capture_announced = 0U;
  g_listen_capture_source = RAW_CAPTURE_SOURCE_NONE;
  g_listen_capture_channel = DEMO_CHANNEL_MIX;
  g_listen_capture_samples = 0U;
  g_listen_capture_armed = 0U;
  g_listen_capture_ready = 0U;
  g_listen_capture_announced = 0U;
  g_demo_diag.raw_capture_source = source;
  g_demo_diag.raw_capture_frames = 0U;
  g_demo_diag.raw_capture_ready = 0U;
  taskEXIT_CRITICAL();
}

static void raw_capture_append(uint32_t source, const uint32_t *words, uint32_t frame_count)
{
  uint32_t have, take;
  if (!g_raw_capture_armed || g_raw_capture_source != source || words == NULL || frame_count == 0U) return;
  have = g_raw_capture_frames;
  if (have >= DEMO_RAW_CAPTURE_FRAMES) return;
  take = DEMO_RAW_CAPTURE_FRAMES - have;
  if (take > frame_count) take = frame_count;
  memcpy(&g_raw_capture_words[have * 2U], words, take * 2U * sizeof(uint32_t));
  __DMB();
  have += take;
  g_raw_capture_frames = have;
  g_demo_diag.raw_capture_frames = have;
  if (have >= DEMO_RAW_CAPTURE_FRAMES) {
    g_raw_capture_armed = 0U;
    g_raw_capture_ready = 1U;
    g_demo_diag.raw_capture_ready = 1U;
  }
}

static uint16_t crc16_update(uint16_t crc, const uint8_t *data, uint32_t len)
{
  if (data == NULL) return crc;
  for (uint32_t i = 0U; i < len; ++i) {
    crc ^= (uint16_t)data[i] << 8;
    for (uint32_t b = 0U; b < 8U; ++b)
      crc = (crc & 0x8000U) ? (uint16_t)((crc << 1) ^ 0x1021U) : (uint16_t)(crc << 1);
  }
  return crc;
}

static void raw_capture_send(void)
{
  /* Frame layout: A5 5A 'C' 'P', version, source, reserved16,
   * sample_rate32, frames32, words_per_frame32, payload_bytes32,
   * firmware_decode_mode32, raw little-endian uint32 payload, crc16. */
  uint8_t header[28];
  uint8_t crc_bytes[2];
  uint32_t o = 0U;
  uint32_t payload_bytes = DEMO_RAW_CAPTURE_WORDS * sizeof(uint32_t);
  uint16_t crc = 0xFFFFU;
  HAL_StatusTypeDef st;
  if (!g_raw_capture_ready) {
    console_write("ERR capture not ready; use CAPTURE BT or CAPTURE TC first\r\n");
    return;
  }

  header[o++] = 0xA5U; header[o++] = 0x5AU; header[o++] = 'C'; header[o++] = 'P';
  header[o++] = 1U;
  header[o++] = (uint8_t)g_raw_capture_source;
  put_u16_le(&header[o], 0U); o += 2U;
  put_u32_le(&header[o], DEMO_AUDIO_RATE_HZ); o += 4U;
  put_u32_le(&header[o], DEMO_RAW_CAPTURE_FRAMES); o += 4U;
  put_u32_le(&header[o], 2U); o += 4U;
  put_u32_le(&header[o], payload_bytes); o += 4U;
  put_u32_le(&header[o], g_demo_control.pcm_decode_mode); o += 4U;

  crc = crc16_update(crc, &header[4], o - 4U);
  crc = crc16_update(crc, (const uint8_t *)g_raw_capture_words, payload_bytes);
  put_u16_le(crc_bytes, crc);

  console_write("CAPTURE DUMP BEGIN source=");
  console_write(raw_capture_source_name(g_raw_capture_source));
  console_write(" frames="); console_u32(DEMO_RAW_CAPTURE_FRAMES);
  console_write(" rawWords="); console_u32(DEMO_RAW_CAPTURE_WORDS);
  console_write("\r\n");
  st = uart2_send_binary_chunked(header, o);
  if (st == HAL_OK) st = uart2_send_binary_chunked((const uint8_t *)g_raw_capture_words, payload_bytes);
  if (st == HAL_OK) st = uart2_send_binary_chunked(crc_bytes, 2U);
  if (st == HAL_OK) {
    g_demo_diag.console_tx_bytes += o + payload_bytes + 2U;
    g_demo_diag.raw_capture_dumps++;
    console_write("\r\nCAPTURE DUMP END\r\n");
  } else {
    console_write("\r\nERR capture dump UART failure\r\n");
  }
}


static const char *listen_channel_name(uint32_t channel)
{
  if (channel == DEMO_CHANNEL_LEFT) return "LEFT";
  if (channel == DEMO_CHANNEL_RIGHT) return "RIGHT";
  return "MIX";
}

static void listen_capture_arm(uint32_t source, uint32_t channel)
{
  taskENTER_CRITICAL();
  /* SRAM2 is shared with the exact-word capture. */
  g_raw_capture_armed = 0U;
  g_raw_capture_ready = 0U;
  g_raw_capture_frames = 0U;
  g_listen_capture_source = source;
  g_listen_capture_channel = channel;
  g_listen_capture_samples = 0U;
  g_listen_capture_armed = (source == RAW_CAPTURE_SOURCE_BT || source == RAW_CAPTURE_SOURCE_TC) ? 1U : 0U;
  g_listen_capture_ready = 0U;
  g_listen_capture_announced = 0U;
  g_demo_diag.listen_capture_source = source;
  g_demo_diag.listen_capture_channel = channel;
  g_demo_diag.listen_capture_samples = 0U;
  g_demo_diag.listen_capture_ready = 0U;
  taskEXIT_CRITICAL();
}

static int16_t select_channel_sample(int16_t left, int16_t right, uint32_t channel)
{
  if (channel == DEMO_CHANNEL_LEFT) return left;
  if (channel == DEMO_CHANNEL_RIGHT) return right;
  return Audio_MixStereoToMono(left, right);
}

static void listen_capture_append(uint32_t source, const int16_t *left, const int16_t *right, uint32_t frame_count)
{
  uint32_t have, take;
  if (!g_listen_capture_armed || g_listen_capture_source != source ||
      left == NULL || right == NULL || frame_count == 0U) return;
  have = g_listen_capture_samples;
  if (have >= DEMO_LISTEN_CAPTURE_SAMPLES) return;
  take = DEMO_LISTEN_CAPTURE_SAMPLES - have;
  if (take > frame_count) take = frame_count;
  for (uint32_t i = 0U; i < take; ++i) {
    g_listen_capture_pcm[have + i] = select_channel_sample(left[i], right[i], g_listen_capture_channel);
  }
  __DMB();
  have += take;
  g_listen_capture_samples = have;
  g_demo_diag.listen_capture_samples = have;
  if (have >= DEMO_LISTEN_CAPTURE_SAMPLES) {
    g_listen_capture_armed = 0U;
    g_listen_capture_ready = 1U;
    g_demo_diag.listen_capture_ready = 1U;
  }
}

static void listen_capture_send(void)
{
  /* Frame layout: A5 5A 'A' 'U', version, source, channel, decode,
   * sample_rate32, samples32, payload_bytes32, int16 PCM payload, crc16.
   * CRC covers bytes after the 4-byte sync plus the payload. */
  uint8_t header[20];
  uint8_t crc_bytes[2];
  uint32_t o = 0U;
  uint32_t payload_bytes = DEMO_LISTEN_CAPTURE_SAMPLES * sizeof(int16_t);
  uint16_t crc = 0xFFFFU;
  HAL_StatusTypeDef st;
  if (!g_listen_capture_ready) {
    console_write("ERR listen capture not ready; use RECORD BT/TC LEFT/RIGHT/MIX first\r\n");
    return;
  }

  header[o++] = 0xA5U; header[o++] = 0x5AU; header[o++] = 'A'; header[o++] = 'U';
  header[o++] = 1U;
  header[o++] = (uint8_t)g_listen_capture_source;
  header[o++] = (uint8_t)g_listen_capture_channel;
  header[o++] = (uint8_t)g_demo_control.pcm_decode_mode;
  put_u32_le(&header[o], DEMO_AUDIO_RATE_HZ); o += 4U;
  put_u32_le(&header[o], DEMO_LISTEN_CAPTURE_SAMPLES); o += 4U;
  put_u32_le(&header[o], payload_bytes); o += 4U;

  crc = crc16_update(crc, &header[4], o - 4U);
  crc = crc16_update(crc, (const uint8_t *)g_listen_capture_pcm, payload_bytes);
  put_u16_le(crc_bytes, crc);

  console_write("RECORD DUMP BEGIN source=");
  console_write(raw_capture_source_name(g_listen_capture_source));
  console_write(" channel="); console_write(listen_channel_name(g_listen_capture_channel));
  console_write(" samples="); console_u32(DEMO_LISTEN_CAPTURE_SAMPLES);
  console_write("\r\n");
  st = uart2_send_binary_chunked(header, o);
  if (st == HAL_OK) st = uart2_send_binary_chunked((const uint8_t *)g_listen_capture_pcm, payload_bytes);
  if (st == HAL_OK) st = uart2_send_binary_chunked(crc_bytes, 2U);
  if (st == HAL_OK) {
    g_demo_diag.console_tx_bytes += o + payload_bytes + 2U;
    g_demo_diag.listen_capture_dumps++;
    console_write("\r\nRECORD DUMP END\r\n");
  } else {
    console_write("\r\nERR record dump UART failure\r\n");
  }
}

static bool console_rx_pop(uint8_t *out)
{
  uint16_t tail;
  if (out == NULL) return false;
  tail = g_console_rx_tail;
  if (tail == g_console_rx_head) return false;
  *out = g_console_rx_ring[tail];
  g_console_rx_tail = (uint16_t)((tail + 1U) & (UART_RX_RING_SIZE - 1U));
  return true;
}

static bool bt_rx_pop(uint8_t *out)
{
  uint16_t tail;
  if (out == NULL) return false;
  tail = g_bt_rx_tail;
  if (tail == g_bt_rx_head) return false;
  *out = g_bt_rx_ring[tail];
  g_bt_rx_tail = (uint16_t)((tail + 1U) & (UART_RX_RING_SIZE - 1U));
  return true;
}

static uint8_t capture_depth(uint8_t head, uint8_t tail)
{
  return (uint8_t)((head - tail) & CAPTURE_RING_MASK);
}

static void capture_push_bt_from_isr(const uint32_t *words)
{
  const uint8_t head = g_bt_cap_head;
  const uint8_t next = (uint8_t)((head + 1U) & CAPTURE_RING_MASK);
  if (next == g_bt_cap_tail) {
    g_demo_diag.bt_dma_overruns++;
    return;
  }
  memcpy(g_bt_capture[head].words, words, sizeof(g_bt_capture[head].words));
  __DMB();
  g_bt_cap_head = next;
  {
    uint8_t d = capture_depth(next, g_bt_cap_tail);
    g_demo_diag.bt_capture_depth = d;
    if (d > g_demo_diag.bt_capture_max_depth) g_demo_diag.bt_capture_max_depth = d;
  }
}

static void capture_push_tc_from_isr(const uint32_t *words)
{
  const uint8_t head = g_tc_cap_head;
  const uint8_t next = (uint8_t)((head + 1U) & CAPTURE_RING_MASK);
  if (next == g_tc_cap_tail) {
    g_demo_diag.tc_dma_overruns++;
    return;
  }
  memcpy(g_tc_capture[head].words, words, sizeof(g_tc_capture[head].words));
  __DMB();
  g_tc_cap_head = next;
  {
    uint8_t d = capture_depth(next, g_tc_cap_tail);
    g_demo_diag.tc_capture_depth = d;
    if (d > g_demo_diag.tc_capture_max_depth) g_demo_diag.tc_capture_max_depth = d;
  }
}

static bool capture_pop_bt(uint32_t *dst)
{
  uint8_t tail = g_bt_cap_tail;
  if (tail == g_bt_cap_head || dst == NULL) return false;
  memcpy(dst, g_bt_capture[tail].words, sizeof(g_bt_capture[tail].words));
  __DMB();
  g_bt_cap_tail = (uint8_t)((tail + 1U) & CAPTURE_RING_MASK);
  g_demo_diag.bt_capture_depth = capture_depth(g_bt_cap_head, g_bt_cap_tail);
  return true;
}

static bool capture_pop_tc(uint32_t *dst)
{
  uint8_t tail = g_tc_cap_tail;
  if (tail == g_tc_cap_head || dst == NULL) return false;
  memcpy(dst, g_tc_capture[tail].words, sizeof(g_tc_capture[tail].words));
  __DMB();
  g_tc_cap_tail = (uint8_t)((tail + 1U) & CAPTURE_RING_MASK);
  g_demo_diag.tc_capture_depth = capture_depth(g_tc_cap_head, g_tc_cap_tail);
  return true;
}

static void start_uart_rx_interrupts(void)
{
  HAL_StatusTypeDef a, b;
  g_console_rx_head = g_console_rx_tail = 0U;
  g_bt_rx_head = g_bt_rx_tail = 0U;

  a = HAL_UART_Receive_IT(&huart2, &g_console_rx_irq_byte, 1U);
  if (a == HAL_OK) g_demo_diag.console_rx_irq_armed = 1U;
  else { g_demo_diag.console_rx_irq_armed = 0U; Demo_SetError(0xC220U | ((uint32_t)a & 0xFU)); }

  b = HAL_UART_Receive_IT(&huart1, &g_bt_rx_irq_byte, 1U);
  if (b == HAL_OK) g_demo_diag.bt_rx_irq_armed = 1U;
  else { g_demo_diag.bt_rx_irq_armed = 0U; Demo_SetError(0xC230U | ((uint32_t)b & 0xFU)); }
}

static uint32_t uabs32(int32_t v)
{
  if (v < 0) v = -v;
  return (uint32_t)v;
}

static void console_write(const char *s)
{
  /* R25: console TX is deliberately independent of HAL_UART_Transmit()'s
   * wall-clock timeout.  Under active SAI DMA the higher-priority AudioTask
   * can pre-empt ControlTask for long enough that a perfectly healthy USART2
   * transmission was previously abandoned as a timeout.  Poll TXE directly
   * instead: AudioTask/SAI IRQs may pre-empt us, but the byte is never dropped
   * merely because wall time elapsed while this lower-priority task slept. */
  if (s == NULL) return;
  if ((RCC->APB1ENR1 & RCC_APB1ENR1_USART2EN) == 0U) return;
  if ((USART2->CR1 & USART_CR1_UE) == 0U) return;
  while (*s != '\0') {
    uint32_t guard = 8000000U;
    while (((USART2->ISR & USART_ISR_TXE) == 0U) && guard-- > 0U) {
      /* Higher-priority audio work remains fully pre-emptive. */
    }
    if ((USART2->ISR & USART_ISR_TXE) == 0U) {
      g_r21_tx_failures++;
      g_demo_diag.console_uart_errors++;
      return;
    }
    USART2->TDR = (uint8_t)(*s++);
    g_demo_diag.console_tx_bytes++;
  }
}

/* Robust binary UART sender for capture downloads.  One giant 32-KiB
 * HAL_UART_Transmit() can hit its single overall timeout when the RTOS preempts
 * the ControlTask.  Small chunks give each piece its own timeout and make the
 * Windows VCP transfer deterministic at 115200 baud. */
static HAL_StatusTypeDef uart2_send_binary_chunked(const uint8_t *data, uint32_t len)
{
  uint32_t sent = 0U;
  if (data == NULL && len != 0U) return HAL_ERROR;
  while (sent < len) {
    uint32_t remain = len - sent;
    uint16_t n = (uint16_t)((remain > 256U) ? 256U : remain);
    HAL_StatusTypeDef st = HAL_ERROR;
    for (uint32_t retry = 0U; retry < 5U; ++retry) {
      st = HAL_UART_Transmit(&huart2, (uint8_t *)&data[sent], n, 1000U);
      if (st == HAL_OK) break;
      vTaskDelay(pdMS_TO_TICKS(1U));
    }
    if (st != HAL_OK) return st;
    sent += n;
    if ((sent & 0x7FFU) == 0U) taskYIELD();
  }
  return HAL_OK;
}

static void console_u32(uint32_t v)
{
  char b[11];
  uint32_t n = 0U;
  if (v == 0U) { console_write("0"); return; }
  while (v > 0U && n < sizeof(b)) { b[n++] = (char)('0' + (v % 10U)); v /= 10U; }
  while (n > 0U) { char c[2] = { b[--n], '\0' }; console_write(c); }
}

static void console_hex8(uint32_t v)
{
  static const char h[] = "0123456789ABCDEF";
  char b[5] = {'0','x','0','0','\0'};
  b[2] = h[(v >> 4U) & 0xFU];
  b[3] = h[v & 0xFU];
  console_write(b);
}


static void dwt_enable(void)
{
#if defined(DWT) && defined(CoreDebug)
  CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;
  DWT->CYCCNT = 0U;
  DWT->CTRL |= DWT_CTRL_CYCCNTENA_Msk;
#endif
}

static uint32_t dwt_cycles(void)
{
#if defined(DWT)
  return DWT->CYCCNT;
#else
  return 0U;
#endif
}

static uint32_t cycles_to_us(uint32_t cycles)
{
  uint32_t per_us = SystemCoreClock / 1000000U;
  return (per_us != 0U) ? (cycles / per_us) : 0U;
}

static int16_t scale_output_sample(int16_t sample)
{
  /* R26: transparent gain with a cheap soft knee.  R25 hard-clipped any
   * transient above full scale, which can sound gritty when the automatic
   * level rises on quiet material.  The knee starts at ~-2 dBFS and limits
   * gracefully before the int16 rails. */
  int64_t y = ((int64_t)sample * (int64_t)g_demo_control.output_gain_q15) >> 15;
  int64_t a = (y < 0) ? -y : y;
  if (a > 26000LL) {
    a = 26000LL + ((a - 26000LL) >> 2);
    if (a > 31900LL) a = 31900LL;
    y = (y < 0) ? -a : a;
  }
  if (y > 31900LL) y = 31900LL;
  if (y < -31900LL) y = -31900LL;
  return (int16_t)y;
}

static uint32_t output_sample_depth(void)
{
  return (uint32_t)(g_output_write_count - g_output_read_count);
}

static void output_queue_reset(void)
{
  taskENTER_CRITICAL();
  g_output_write_count = 0U;
  g_output_read_count = 0U;
  g_output_phase_q16 = 0U;
  g_output_step_q16 = OUTPUT_STEP_Q16_ONE;
  g_output_depth_lp_q8 = OUTPUT_TARGET_SAMPLES << 8;
  g_output_fade_q15 = 0U;
  g_output_depth_min_samples = 0xFFFFFFFFUL;
  g_output_depth_max_samples = 0U;
  g_output_playback_primed = 0U;
  g_r21_have_last_good_output = 0U;
  g_r21_conceal_streak = 0U;
  taskEXIT_CRITICAL();
  g_demo_diag.speaker_queue_depth = 0U;
}

/* Bluetooth/SAI1A RX and SAI2B TX are both derived from the same PLLSAI1
 * kernel in this board configuration.  Resampling that path only adds
 * artificial FM/beat-note modulation.  Telecoil/SAI2A is synchronized internally to SAI1A and
 * can be independently clocked, so only that path uses the slow elastic PLL. */
static bool output_clock_locked_path(void)
{
  if (g_demo_control.direct_source == DEMO_DIRECT_BT) return true;
  if (g_demo_control.direct_source == DEMO_DIRECT_TC) return false;
  return g_demo_diag.active_source == 2U; /* normal routed Bluetooth */
}

static int16_t apply_output_fade(int16_t sample)
{
  uint32_t f = g_output_fade_q15;
  int32_t y;
  if (f < 32768U) {
    f += OUTPUT_FADE_STEP_Q15;
    if (f > 32768U) f = 32768U;
    g_output_fade_q15 = f;
  }
  y = (int32_t)(((int64_t)sample * (int64_t)f) >> 15);
  if (y > 32767) y = 32767;
  if (y < -32768) y = -32768;
  return (int16_t)y;
}

static void output_depth_track(uint32_t depth)
{
  if (depth < g_output_depth_min_samples) g_output_depth_min_samples = depth;
  if (depth > g_output_depth_max_samples) g_output_depth_max_samples = depth;
}

/* Push one 128-frame mono block without changing its sample values.  Gain is
 * applied only at the final SAI2B render step.  The producer never overwrites
 * unread samples; if the FIFO genuinely fills, the condition is counted. */
static void output_queue_push(const int16_t *mono)
{
  uint32_t w, r, used;
  if ((mono == NULL) || !g_demo_control.output_enabled ||
      !g_demo_diag.speaker_dma_running || g_demo_control.test_tone_step != 0U) return;

  w = g_output_write_count;
  r = g_output_read_count;
  used = (uint32_t)(w - r);
  if (used > OUTPUT_SAMPLE_FIFO_SAMPLES) {
    /* Defensive recovery if counters are ever corrupted. */
    g_demo_diag.speaker_queue_overruns++;
    output_queue_reset();
    return;
  }
  if ((OUTPUT_SAMPLE_FIFO_SAMPLES - used) < DEMO_FRAMES_PER_HALF) {
    g_demo_diag.speaker_queue_overruns++;
    return;
  }

  for (uint32_t i = 0U; i < DEMO_FRAMES_PER_HALF; ++i) {
    g_output_samples[(w + i) & OUTPUT_SAMPLE_FIFO_MASK] = mono[i];
  }
  __DMB();
  g_output_write_count = w + DEMO_FRAMES_PER_HALF;
  {
    uint32_t d_samples = output_sample_depth();
    uint32_t d_blocks = d_samples / DEMO_FRAMES_PER_HALF;
    output_depth_track(d_samples);
    g_demo_diag.speaker_queue_depth = d_blocks;
    if (d_blocks > g_demo_diag.speaker_queue_max_depth) g_demo_diag.speaker_queue_max_depth = d_blocks;
  }
}

/* Exact 1:1 renderer for the clock-locked Bluetooth path.  There is no
 * interpolation and no block-to-block rate correction: every received sample
 * is emitted once, in order.  This removes the low-frequency beat note caused
 * by the R8 controller changing fractional read rate every DMA half-buffer. */
static bool output_queue_pop_exact_isr(int16_t *stereo_dst)
{
  uint32_t r, w, available;
  if (stereo_dst == NULL) return false;
  r = g_output_read_count;
  w = g_output_write_count;
  available = (uint32_t)(w - r);
  if (available < DEMO_FRAMES_PER_HALF) return false;

  for (uint32_t i = 0U; i < DEMO_FRAMES_PER_HALF; ++i) {
    int16_t y = scale_output_sample(g_output_samples[(r + i) & OUTPUT_SAMPLE_FIFO_MASK]);
    y = apply_output_fade(y);
    stereo_dst[2U * i] = y;
    stereo_dst[(2U * i) + 1U] = y;
  }
  r += DEMO_FRAMES_PER_HALF;
  __DMB();
  g_output_read_count = r;
  g_output_phase_q16 = 0U;
  g_output_step_q16 = OUTPUT_STEP_Q16_ONE;
  available = (uint32_t)(w - r);
  output_depth_track(available);
  g_demo_diag.speaker_queue_depth = available / DEMO_FRAMES_PER_HALF;
  return true;
}

/* Slow elastic renderer for the asynchronous telecoil path.  Unlike R8, the
 * controller does NOT react to the 128-sample FIFO sawtooth every 2.67 ms.
 * FIFO depth is low-pass filtered first, then the fractional step is adjusted
 * very slowly.  This avoids audible wow/beat modulation while still absorbing
 * genuine long-term sample-clock mismatch. */
static bool output_queue_pop_async_isr(int16_t *stereo_dst)
{
  uint32_t r, w, available, phase;
  int32_t error_samples, correction, target_step, delta;
  uint32_t step_q16;
  int16_t last_y = 0;

  if (stereo_dst == NULL) return false;
  r = g_output_read_count;
  w = g_output_write_count;
  available = (uint32_t)(w - r);
  if (available < 2U) return false;

  {
    int32_t lp = (int32_t)g_output_depth_lp_q8;
    int32_t instant = (int32_t)(available << 8);
    lp += (instant - lp) >> OUTPUT_DEPTH_LP_SHIFT;
    if (lp < 0) lp = 0;
    g_output_depth_lp_q8 = (uint32_t)lp;
  }
  error_samples = (int32_t)(g_output_depth_lp_q8 >> 8) - (int32_t)OUTPUT_TARGET_SAMPLES;
  correction = error_samples / 32; /* 1 Q16 step per 32-sample long-term depth error */
  if (correction > OUTPUT_STEP_Q16_MAX_CORR) correction = OUTPUT_STEP_Q16_MAX_CORR;
  if (correction < -OUTPUT_STEP_Q16_MAX_CORR) correction = -OUTPUT_STEP_Q16_MAX_CORR;
  target_step = (int32_t)OUTPUT_STEP_Q16_ONE + correction;
  delta = target_step - (int32_t)g_output_step_q16;
  /* Slew at only one Q16 LSB per half-buffer (~15 ppm every 2.67 ms). */
  if (delta > 1) delta = 1;
  else if (delta < -1) delta = -1;
  g_output_step_q16 = (uint32_t)((int32_t)g_output_step_q16 + delta);
  step_q16 = g_output_step_q16;
  phase = g_output_phase_q16;

  for (uint32_t i = 0U; i < DEMO_FRAMES_PER_HALF; ++i) {
    int16_t s0, s1, y;
    int32_t interp;
    uint32_t frac, adv;

    available = (uint32_t)(w - r);
    if (available < 2U) {
      uint32_t rem = DEMO_FRAMES_PER_HALF - i;
      for (uint32_t j = i; j < DEMO_FRAMES_PER_HALF; ++j) {
        int32_t faded = ((int32_t)last_y * (int32_t)(DEMO_FRAMES_PER_HALF - j)) / (int32_t)rem;
        int16_t fy = (int16_t)faded;
        stereo_dst[2U * j] = fy;
        stereo_dst[(2U * j) + 1U] = fy;
      }
      g_output_read_count = r;
      g_output_phase_q16 = phase;
      g_output_playback_primed = 0U;
      g_output_fade_q15 = 0U;
      g_demo_diag.speaker_queue_depth = output_sample_depth() / DEMO_FRAMES_PER_HALF;
      g_demo_diag.speaker_underruns++;
      return true;
    }

    s0 = g_output_samples[r & OUTPUT_SAMPLE_FIFO_MASK];
    s1 = g_output_samples[(r + 1U) & OUTPUT_SAMPLE_FIFO_MASK];
    frac = phase & 0xFFFFU;
    interp = (int32_t)s0 + (int32_t)(((int64_t)((int32_t)s1 - (int32_t)s0) * (int64_t)frac) >> 16);
    if (interp > 32767) interp = 32767;
    if (interp < -32768) interp = -32768;
    y = scale_output_sample((int16_t)interp);
    y = apply_output_fade(y);
    last_y = y;
    stereo_dst[2U * i] = y;
    stereo_dst[(2U * i) + 1U] = y;

    phase += step_q16;
    adv = phase >> 16;
    phase &= 0xFFFFU;
    r += adv;
  }

  __DMB();
  g_output_read_count = r;
  g_output_phase_q16 = phase;
  available = output_sample_depth();
  output_depth_track(available);
  g_demo_diag.speaker_queue_depth = available / DEMO_FRAMES_PER_HALF;
  return true;
}

static bool output_queue_pop_isr(int16_t *stereo_dst)
{
  return output_clock_locked_path() ? output_queue_pop_exact_isr(stereo_dst)
                                    : output_queue_pop_async_isr(stereo_dst);
}

static void speaker_fill_tone_isr(int16_t *dst)
{
  const uint8_t step = (uint8_t)g_demo_control.test_tone_step;
  if (dst == NULL || step == 0U) return;
  for (uint32_t i = 0U; i < DEMO_FRAMES_PER_HALF; ++i) {
    int16_t y = scale_output_sample(k_sine_48[g_tone_phase]);
    dst[2U * i] = y;
    dst[(2U * i) + 1U] = y;
    g_tone_phase = (uint8_t)((g_tone_phase + step) % 48U);
  }
}

/* Called directly from the SAI2B DMA callback.  The half that just completed
 * is safe to rewrite immediately, so this removes an entire RTOS scheduling
 * dependency from the physical speaker stream. */
static void speaker_fill_region_isr(uint32_t start_sample)
{
  int16_t *dst = &g_speaker_tx_dma[start_sample];
  bool have = false;

  if (!g_demo_control.output_enabled) {
    memset(dst, 0, DEMO_OUTPUT_HALF_SAMPLES * sizeof(int16_t));
    return;
  }

  if (g_demo_control.test_tone_step != 0U) {
    speaker_fill_tone_isr(dst);
    memcpy(g_r21_last_good_output, dst, sizeof(g_r21_last_good_output));
    g_r21_have_last_good_output = 1U;
    g_r21_conceal_streak = 0U;
    g_demo_diag.speaker_blocks_output++;
    return;
  }

  if (!g_output_playback_primed) {
    const uint32_t prime = output_clock_locked_path() ? OUTPUT_PRIME_SAMPLES_EXACT
                                                       : OUTPUT_PRIME_SAMPLES_ASYNC;
    if (output_sample_depth() >= prime) {
      g_output_phase_q16 = 0U;
      g_output_step_q16 = OUTPUT_STEP_Q16_ONE;
      g_output_depth_lp_q8 = OUTPUT_TARGET_SAMPLES << 8;
      g_output_fade_q15 = 0U;
      g_output_playback_primed = 1U;
    }
  }
  if (g_output_playback_primed) have = output_queue_pop_isr(dst);

  if (!have) {
    /* A single missed producer deadline used to emit a full block of silence,
     * reset the FIFO and create the very audible random stopping/chop.  Reuse
     * the previous good block with a short decay for up to two halves while
     * the producer catches up.  This does not invent long stretches of audio. */
    if (g_output_playback_primed && g_r21_have_last_good_output &&
        g_r21_conceal_streak < 2U && g_demo_diag.active_source != 0U &&
        g_demo_diag.muted == 0U) {
      uint32_t numer = 2U - g_r21_conceal_streak;
      for (uint32_t i = 0U; i < DEMO_OUTPUT_HALF_SAMPLES; ++i) {
        dst[i] = (int16_t)(((int32_t)g_r21_last_good_output[i] * (int32_t)numer) / 2);
      }
      g_r21_conceal_streak++;
      g_r21_conceal_total++;
      g_demo_diag.speaker_underruns++;
      g_demo_diag.speaker_blocks_output++;
      return;
    }
    memset(dst, 0, DEMO_OUTPUT_HALF_SAMPLES * sizeof(int16_t));
    if (g_output_playback_primed && g_demo_diag.active_source != 0U &&
        g_demo_diag.muted == 0U) {
      g_demo_diag.speaker_underruns++;
      g_output_playback_primed = 0U;
      g_output_fade_q15 = 0U;
      g_r21_conceal_streak = 0U;
    }
  } else {
    memcpy(g_r21_last_good_output, dst, sizeof(g_r21_last_good_output));
    g_r21_have_last_good_output = 1U;
    g_r21_conceal_streak = 0U;
    g_demo_diag.speaker_blocks_output++;
  }
}

static void plot_accumulate(const int16_t *pre, const int16_t *post)
{
  /* R24 decouples capture from transmission: always maintain the latest
   * real-audio snapshot while capture is active, but never transmit the old
   * binary plot protocol.  The dashboard asks for !SP explicitly. */
  if (!g_demo_control.audio_capture_enabled) return;
  if ((pre == NULL) || (post == NULL)) return;
  for (uint32_t i = 0U; i < DEMO_FRAMES_PER_HALF; ++i) {
    if (g_plot_accum_count < PLOT_ACCUM_SAMPLES) {
      g_plot_raw_accum[g_plot_accum_count] = pre[i];
      g_plot_proc_accum[g_plot_accum_count] = post[i];
      g_plot_accum_count++;
    }
    if (g_plot_accum_count >= PLOT_ACCUM_SAMPLES) {
      for (uint32_t k = 0U; k < DEMO_PLOT_SAMPLES; ++k) {
        uint32_t j = k * 4U;
        int32_t raw_sum = (int32_t)g_plot_raw_accum[j] + g_plot_raw_accum[j + 1U] +
                          g_plot_raw_accum[j + 2U] + g_plot_raw_accum[j + 3U];
        int32_t proc_sum = (int32_t)g_plot_proc_accum[j] + g_plot_proc_accum[j + 1U] +
                           g_plot_proc_accum[j + 2U] + g_plot_proc_accum[j + 3U];
        g_plot_raw_snapshot[k] = (int16_t)(raw_sum / 4);
        g_plot_proc_snapshot[k] = (int16_t)(proc_sum / 4);
      }
      /* Retain contiguous downsampled PCM from the same actual audio blocks.
       * Unlike joining sporadic !SP polls, this yields REAL 2048-point
       * time support at 12 kSa/s (5.859375 Hz native resolution). */
      if (g_r262_hd_source != g_demo_diag.active_source) {
        g_r262_hd_valid = 0U;
        g_r262_hd_source = g_demo_diag.active_source;
      }
      memmove(g_r262_hd_buffer, g_r262_hd_buffer + DEMO_PLOT_SAMPLES,
              (R262_HD_SAMPLES - DEMO_PLOT_SAMPLES) * sizeof(int16_t));
      memcpy(g_r262_hd_buffer + R262_HD_SAMPLES - DEMO_PLOT_SAMPLES,
             g_plot_proc_snapshot, DEMO_PLOT_SAMPLES * sizeof(int16_t));
      if (g_r262_hd_valid < R262_HD_SAMPLES) g_r262_hd_valid += DEMO_PLOT_SAMPLES;
      g_r262_hd_seq++;
      g_plot_accum_count = 0U;
      g_plot_snapshot_seq++;
    }
  }
}


/* Transparent reference path: decoded input -> selected channel -> speaker
 * queue. No connectivity controller, mute state, telecoil filter, EQ, AGC,
 * compressor, gate, limiter or source arbitration touches these samples. */
static void direct_process_block(uint32_t source, const int16_t *left, const int16_t *right, uint32_t lrms, uint32_t rrms)
{
  uint32_t in_rms = 0U, in_peak = 0U;
  uint32_t channel;
  int64_t corr = 0;
  uint64_t energy = 0U;
  if (left == NULL || right == NULL || source == DEMO_DIRECT_OFF) return;

  channel = g_demo_control.direct_channel;

  /* R26 transparent stereo fold.
   * R25 selected LEFT/MIX/RIGHT independently every 128-sample block.  Near a
   * decision boundary that could toggle from block to block and create a
   * subtle roughness/clicking.  R26 computes a target weight and slews toward
   * it, preserving stereo information without abrupt topology changes. */
  if (source == DEMO_DIRECT_BT && g_r21_smart_mono_enabled) {
    uint32_t target = 16384U;
    uint32_t sumrms = lrms + rrms;
    for (uint32_t i = 0U; i < DEMO_FRAMES_PER_HALF; ++i) {
      corr += (int64_t)left[i] * (int64_t)right[i];
      energy += (uint64_t)((int32_t)left[i] * (int32_t)left[i]);
      energy += (uint64_t)((int32_t)right[i] * (int32_t)right[i]);
    }
    if (corr < 0 && (uint64_t)(-corr) * 4U > energy) {
      target = (lrms >= rrms) ? 30720U : 2048U;
    } else if (sumrms > 32U) {
      target = (uint32_t)(((uint64_t)lrms * 32768ULL) / (uint64_t)sumrms);
      if (target < 8192U) target = 8192U;
      if (target > 24576U) target = 24576U;
    }
    g_r26_target_left_weight_q15 = target;
    if (g_r26_left_weight_q15 < target) {
      uint32_t d = target - g_r26_left_weight_q15;
      g_r26_left_weight_q15 += (d + 7U) >> 3;
    } else if (g_r26_left_weight_q15 > target) {
      uint32_t d = g_r26_left_weight_q15 - target;
      g_r26_left_weight_q15 -= (d + 7U) >> 3;
    }
    g_demo_diag.bt_mono_selected = DEMO_BT_MONO_AUTO;
    for (uint32_t i = 0U; i < DEMO_FRAMES_PER_HALF; ++i) {
      int32_t wl = (int32_t)g_r26_left_weight_q15;
      int32_t wr = 32768 - wl;
      int32_t y = (((int32_t)left[i] * wl) >> 15) + (((int32_t)right[i] * wr) >> 15);
      if (y > 32767) y = 32767;
      if (y < -32768) y = -32768;
      g_mono[i] = (int16_t)y;
    }
  } else {
    for (uint32_t i = 0U; i < DEMO_FRAMES_PER_HALF; ++i) {
      g_mono[i] = select_channel_sample(left[i], right[i], channel);
    }
  }
  metrics_s16(g_mono, DEMO_FRAMES_PER_HALF, &in_rms, NULL, &in_peak);
  memcpy(g_demo_processed_mono, g_mono, sizeof(g_demo_processed_mono));
  if (source == DEMO_DIRECT_TC && g_r27_tc_clean_enabled) {
    TcCleanup_Block(&g_r27_tc_filter,g_mono,g_demo_processed_mono,DEMO_FRAMES_PER_HALF);
  }

  /* R26 conservative automatic level recovery.  The dominant R25 distortion
   * mechanism was not insufficient gain; it was transport backlog.  Keep gain
   * movement slow, cap it at 4x, and reserve ~2 dBFS transient headroom. */
  if (g_r21_auto_level_enabled) {
    if ((g_r24_gain_update_div++ & 0x07U) == 0U) {
      uint32_t desired = 32768U;
      if (in_rms >= 64U) {
        uint64_t g = ((uint64_t)3600U * 32768ULL) / (uint64_t)in_rms;
        if (g > (4ULL * 32768ULL)) g = 4ULL * 32768ULL;
        if (in_peak > 0U) {
          uint64_t peak_safe = ((uint64_t)26000U * 32768ULL) / (uint64_t)in_peak;
          if (g > peak_safe) g = peak_safe;
        }
        if (g < 16384ULL) g = 16384ULL;
        desired = (uint32_t)g;
      }
      if (desired < g_r21_auto_gain_q15) {
        g_r21_auto_gain_q15 = (g_r21_auto_gain_q15 + desired) / 2U;
      } else {
        g_r21_auto_gain_q15 = (31U * g_r21_auto_gain_q15 + desired) / 32U;
      }
    }
    g_demo_control.output_gain_q15 = g_r21_auto_gain_q15;
  }

  g_demo_diag.active_source = (source == DEMO_DIRECT_BT) ? 2U : 1U;
  g_demo_diag.muted = 0U;
  g_demo_diag.dsp_input_rms = in_rms;
  uint32_t actual_out_rms=0U, actual_out_peak=0U;
  if (!g_group_guard_bypass)
    GroupFeatures_Process(&g_group_features, g_demo_processed_mono,
                          g_demo_processed_mono, DEMO_FRAMES_PER_HALF);
  g_group_features.processed_blocks++;
  metrics_s16(g_demo_processed_mono,DEMO_FRAMES_PER_HALF,&actual_out_rms,NULL,&actual_out_peak);
  g_demo_diag.dsp_output_rms = actual_out_rms;
  g_demo_diag.dsp_input_peak = in_peak;
  g_demo_diag.dsp_output_peak = actual_out_peak;
  g_demo_diag.dsp_gain_q15 = 32768;
  g_demo_diag.dsp_last_us = 0U;
  g_demo_diag.processed_output_peak = actual_out_peak;

  output_queue_push(g_demo_processed_mono);
  plot_accumulate(g_mono, g_demo_processed_mono);
}

static void process_selected_audio(const int16_t *pre, uint32_t source)
{
  uint32_t in_rms = 0U, in_peak = 0U, out_rms = 0U, out_peak = 0U;
  uint32_t start_cycles, elapsed_us;
  if (pre == NULL) return;

  if (source != g_last_output_source) {
    output_queue_reset();
    g_plot_accum_count = 0U;
    (void)AudioDynamics_Reset(&g_dynamics);
    g_last_output_source = source;
  }

  metrics_s16(pre, DEMO_FRAMES_PER_HALF, &in_rms, NULL, &in_peak);
  start_cycles = dwt_cycles();
  if (g_demo_control.dsp_enabled) {
    (void)AudioDynamics_ProcessBlock(&g_dynamics, pre, g_demo_processed_mono,
                                     DEMO_FRAMES_PER_HALF, g_demo_diag.muted != 0U);
  } else if (g_demo_diag.muted) {
    memset(g_demo_processed_mono, 0, sizeof(g_demo_processed_mono));
  } else {
    memcpy(g_demo_processed_mono, pre, sizeof(g_demo_processed_mono));
  }
  if (!g_group_guard_bypass)
    GroupFeatures_Process(&g_group_features, g_demo_processed_mono,
                          g_demo_processed_mono, DEMO_FRAMES_PER_HALF);
  g_group_features.processed_blocks++;
  elapsed_us = cycles_to_us(dwt_cycles() - start_cycles);
  metrics_s16(g_demo_processed_mono, DEMO_FRAMES_PER_HALF, &out_rms, NULL, &out_peak);

  g_demo_diag.dsp_blocks++;
  g_demo_diag.dsp_input_rms = in_rms;
  g_demo_diag.dsp_output_rms = out_rms;
  g_demo_diag.dsp_input_peak = in_peak;
  g_demo_diag.dsp_output_peak = out_peak;
  g_demo_diag.dsp_gain_q15 = g_demo_control.dsp_enabled ? g_dynamics.current_gain_q15 : 32768;
  g_demo_diag.dsp_last_us = elapsed_us;
  if (elapsed_us > g_demo_diag.dsp_max_us) g_demo_diag.dsp_max_us = elapsed_us;
  g_demo_diag.processed_output_peak = out_peak;
  if (g_demo_control.dsp_enabled) {
    g_demo_diag.dsp_limited_samples += g_dynamics.last_limiter_samples;
  }

  output_queue_push(g_demo_processed_mono);
  plot_accumulate(g_r27_pre_source, g_demo_processed_mono);
}

static uint16_t crc16_ccitt(const uint8_t *data, uint32_t len)
{
  return crc16_update(0xFFFFU, data, len);
}

static void put_u16_le(uint8_t *p, uint16_t v)
{
  p[0] = (uint8_t)(v & 0xFFU); p[1] = (uint8_t)(v >> 8);
}

static void put_u32_le(uint8_t *p, uint32_t v)
{
  p[0] = (uint8_t)(v & 0xFFU); p[1] = (uint8_t)((v >> 8) & 0xFFU);
  p[2] = (uint8_t)((v >> 16) & 0xFFU); p[3] = (uint8_t)((v >> 24) & 0xFFU);
}

static void update_task_rates(uint32_t now)
{
  uint32_t dt = now - g_last_evidence_ms;
  if (dt < 1000U) return;
  if (g_last_evidence_ms == 0U) dt = 1000U;
  g_demo_diag.control_task_rate_hz = ((g_demo_diag.control_task_loops - g_last_rate_ctl) * 1000U) / dt;
  g_demo_diag.audio_task_rate_hz = ((g_demo_diag.audio_task_loops - g_last_rate_aud) * 1000U) / dt;
  g_demo_diag.bt_task_rate_hz = ((g_demo_diag.bt_task_loops - g_last_rate_bt) * 1000U) / dt;
  g_demo_diag.background_task_rate_hz = ((g_demo_diag.background_task_loops - g_last_rate_bg) * 1000U) / dt;
  g_last_rate_ctl = g_demo_diag.control_task_loops;
  g_last_rate_aud = g_demo_diag.audio_task_loops;
  g_last_rate_bt = g_demo_diag.bt_task_loops;
  g_last_rate_bg = g_demo_diag.background_task_loops;
  g_last_evidence_ms = now;
}

static void print_evidence(void)
{
  console_write("EVIDENCE RTOS ctlHz="); console_u32(g_demo_diag.control_task_rate_hz);
  console_write(" audHz="); console_u32(g_demo_diag.audio_task_rate_hz);
  console_write(" btHz="); console_u32(g_demo_diag.bt_task_rate_hz);
  console_write(" bgHz="); console_u32(g_demo_diag.background_task_rate_hz);
  console_write(" load="); console_u32(g_demo_control.background_load_enabled);
  console_write(" stack[ctl="); console_u32(g_demo_diag.control_stack_high_water);
  console_write(" aud="); console_u32(g_demo_diag.audio_stack_high_water);
  console_write(" bt="); console_u32(g_demo_diag.bt_stack_high_water);
  console_write(" bg="); console_u32(g_demo_diag.background_stack_high_water);
  console_write("]\r\n");

  console_write("EVIDENCE DSP on="); console_u32(g_demo_control.dsp_enabled);
  console_write(" src="); console_write(source_name(g_demo_diag.active_source));
  console_write(" inRMS="); console_u32(g_demo_diag.dsp_input_rms);
  console_write(" outRMS="); console_u32(g_demo_diag.dsp_output_rms);
  console_write(" inPk="); console_u32(g_demo_diag.dsp_input_peak);
  console_write(" outPk="); console_u32(g_demo_diag.dsp_output_peak);
  console_write(" gainQ15="); console_u32((uint32_t)g_demo_diag.dsp_gain_q15);
  console_write(" us="); console_u32(g_demo_diag.dsp_last_us);
  console_write(" maxUs="); console_u32(g_demo_diag.dsp_max_us);
  console_write(" budgetUs=2667 limited="); console_u32(g_demo_diag.dsp_limited_samples);
  console_write(" eq="); console_u32(g_dynamics.resonance_eq_enabled ? 1U : 0U);
  console_write(" comp="); console_u32(g_dynamics.compressor_enabled ? 1U : 0U);
  console_write(" meanAbs="); console_u32(g_dynamics.last_mean_abs);
  console_write("\r\n");

  console_write("EVIDENCE AUDIO rx="); console_write(g_demo_diag.audio_dma_running ? "RUN" : "OFF");
  console_write(" tx="); console_write(g_demo_diag.speaker_dma_running ? "RUN" : "OFF");
  console_write(" btCb="); console_u32(g_demo_diag.bt_half_callbacks + g_demo_diag.bt_full_callbacks);
  console_write(" btBlocks="); console_u32(g_demo_diag.bt_blocks_processed);
  console_write(" btOver="); console_u32(g_demo_diag.bt_dma_overruns);
  console_write(" btCap="); console_u32(g_demo_diag.bt_capture_depth);
  console_write(" lrms="); console_u32(g_demo_diag.bt_left_rms);
  console_write(" rrms="); console_u32(g_demo_diag.bt_right_rms);
  console_write(" mono="); console_u32(g_demo_diag.bt_mono_selected);
  console_write(" tcCb="); console_u32(g_demo_diag.tc_half_callbacks + g_demo_diag.tc_full_callbacks);
  console_write(" tcBlocks="); console_u32(g_demo_diag.tc_blocks_processed);
  console_write(" tcOver="); console_u32(g_demo_diag.tc_dma_overruns);
  console_write(" tcCap="); console_u32(g_demo_diag.tc_capture_depth);
  console_write(" outBlocks="); console_u32(g_demo_diag.speaker_blocks_output);
  console_write(" under="); console_u32(g_demo_diag.speaker_underruns);
  console_write(" qOver="); console_u32(g_demo_diag.speaker_queue_overruns);
  console_write(" qDepth="); console_u32(g_demo_diag.speaker_queue_depth);
  console_write(" levelQ15="); console_u32(g_demo_control.output_gain_q15);
  console_write(" toneHz="); console_u32(g_demo_diag.test_tone_hz);
  console_write(" pcmMode="); console_u32(g_demo_control.pcm_decode_mode);
  console_write(" rawCap="); console_u32(g_raw_capture_frames);
  console_write(" rawReady="); console_u32(g_raw_capture_ready);
  console_write("\r\n");

  console_write("EVIDENCE FSM mode=M"); console_u32(g_demo_diag.mode);
  console_write(" src="); console_write(source_name(g_demo_diag.active_source));
  console_write(" sw="); console_u32(g_demo_diag.source_switches);
  console_write(" fault="); console_u32(g_demo_diag.fault);
  console_write(" mute="); console_u32(g_demo_diag.muted);
  console_write(" svcMaxMs="); console_u32(g_demo_diag.max_service_interval_ms);
  console_write(" miss="); console_u32(g_demo_diag.service_deadline_misses);
  console_write("\r\n");
  g_demo_diag.evidence_reports++;
}

static void __attribute__((unused)) send_plot_frame(uint32_t now)
{
  enum { META_BYTES = 60U, SAMPLE_BYTES = DEMO_PLOT_SAMPLES * 4U, PAYLOAD_BYTES = META_BYTES + SAMPLE_BYTES };
  static uint8_t frame[8U + PAYLOAD_BYTES + 2U];
  int16_t raw[DEMO_PLOT_SAMPLES], proc[DEMO_PLOT_SAMPLES];
  uint32_t seq;
  uint32_t o = 0U;
  uint8_t flags = 0U;
  uint16_t crc;
  if (!g_demo_control.plot_enabled || g_plot_snapshot_seq == g_plot_last_sent_seq) return;
  if ((uint32_t)(now - g_last_plot_send_ms) < 250U) return;

  taskENTER_CRITICAL();
  seq = g_plot_snapshot_seq;
  memcpy(raw, g_plot_raw_snapshot, sizeof(raw));
  memcpy(proc, g_plot_proc_snapshot, sizeof(proc));
  taskEXIT_CRITICAL();

  frame[o++] = 0xA5U; frame[o++] = 0x5AU; frame[o++] = 'H'; frame[o++] = 'D';
  frame[o++] = 1U; frame[o++] = 1U;
  put_u16_le(&frame[o], (uint16_t)PAYLOAD_BYTES); o += 2U;
  put_u32_le(&frame[o], seq); o += 4U;
  put_u32_le(&frame[o], now); o += 4U;
  put_u32_le(&frame[o], DEMO_PLOT_RATE_HZ); o += 4U;
  frame[o++] = (uint8_t)g_demo_diag.active_source;
  if (g_demo_control.dsp_enabled) flags |= 0x01U;
  if (g_demo_control.output_enabled) flags |= 0x02U;
  if (g_demo_diag.audio_dma_running) flags |= 0x04U;
  if (g_demo_control.background_load_enabled) flags |= 0x08U;
  frame[o++] = flags;
  put_u16_le(&frame[o], 0U); o += 2U;
  put_u32_le(&frame[o], g_demo_diag.dsp_input_rms); o += 4U;
  put_u32_le(&frame[o], g_demo_diag.dsp_output_rms); o += 4U;
  put_u32_le(&frame[o], g_demo_diag.dsp_input_peak); o += 4U;
  put_u32_le(&frame[o], g_demo_diag.dsp_output_peak); o += 4U;
  put_u32_le(&frame[o], (uint32_t)g_demo_diag.dsp_gain_q15); o += 4U;
  put_u32_le(&frame[o], g_demo_diag.dsp_last_us); o += 4U;
  put_u32_le(&frame[o], g_demo_diag.dsp_max_us); o += 4U;
  put_u32_le(&frame[o], g_demo_diag.control_task_rate_hz); o += 4U;
  put_u32_le(&frame[o], g_demo_diag.audio_task_rate_hz); o += 4U;
  put_u32_le(&frame[o], g_demo_diag.bt_task_rate_hz); o += 4U;
  put_u32_le(&frame[o], g_demo_diag.background_task_rate_hz); o += 4U;
  for (uint32_t i = 0U; i < DEMO_PLOT_SAMPLES; ++i) { put_u16_le(&frame[o], (uint16_t)raw[i]); o += 2U; }
  for (uint32_t i = 0U; i < DEMO_PLOT_SAMPLES; ++i) { put_u16_le(&frame[o], (uint16_t)proc[i]); o += 2U; }
  crc = crc16_ccitt(&frame[4], o - 4U);
  put_u16_le(&frame[o], crc); o += 2U;
  if (HAL_UART_Transmit(&huart2, frame, (uint16_t)o, 1000U) == HAL_OK) {
    g_demo_diag.console_tx_bytes += o;
    g_demo_diag.plot_frames_sent++;
    g_plot_last_sent_seq = seq;
    g_last_plot_send_ms = now;
  }
}

static char upper_ascii(char c)
{
  if (c >= 'a' && c <= 'z') return (char)(c - ('a' - 'A'));
  return c;
}

static bool line_eq(const char *line, const char *lit)
{
  size_t b = 0U, e, i = 0U;
  if (line == NULL || lit == NULL) return false;
  e = strlen(line);
  while (b < e && (line[b] == ' ' || line[b] == '\t')) b++;
  while (e > b && (line[e-1U] == ' ' || line[e-1U] == '\t')) e--;
  while ((b+i) < e && lit[i] != '\0') {
    if (upper_ascii(line[b+i]) != upper_ascii(lit[i])) return false;
    i++;
  }
  return ((b+i) == e && lit[i] == '\0');
}

static const char *source_name(uint32_t s)
{
  if (s == 1U) return "TC";
  if (s == 2U) return "BT";
  return "NONE";
}

void Demo_SetStage(uint32_t stage) { g_demo_diag.stage = stage; }
void Demo_SetError(uint32_t error_code) { if (g_demo_diag.error_code == 0U) g_demo_diag.error_code = error_code; }

void Demo_ConsoleEarly(const char *message) { console_write(message); }

static void panic_putc(char c)
{
  uint32_t guard = 2000000U;
  if ((RCC->APB1ENR1 & RCC_APB1ENR1_USART2EN) == 0U) return;
  if ((USART2->CR1 & USART_CR1_UE) == 0U) return;
  while (((USART2->ISR & USART_ISR_TXE) == 0U) && guard-- > 0U) { }
  if ((USART2->ISR & USART_ISR_TXE) != 0U) USART2->TDR = (uint8_t)c;
}

static void panic_puts(const char *s)
{
  if (s == NULL) return;
  while (*s != '\0') panic_putc(*s++);
}

void Demo_ConsoleRaw(const char *message)
{
  panic_puts(message);
}

void Demo_ConsolePanic(const char *tag, uint32_t code)
{
  static const char h[] = "0123456789ABCDEF";
  panic_puts("\r\nPANIC ");
  panic_puts(tag != NULL ? tag : "FAULT");
  panic_puts(" code=0x");
  for (int shift = 28; shift >= 0; shift -= 4) panic_putc(h[(code >> (uint32_t)shift) & 0xFU]);
  panic_puts("\r\n");
}

static void refresh_clocks(void)
{
  uint32_t w = 0U;
  g_demo_diag.sysclk_hz = HAL_RCC_GetSysClockFreq();
  g_demo_diag.hclk_hz = HAL_RCC_GetHCLKFreq();
  g_demo_diag.pclk1_hz = HAL_RCC_GetPCLK1Freq();
  g_demo_diag.pclk2_hz = HAL_RCC_GetPCLK2Freq();
  g_demo_diag.usart1_clk_hz = HAL_RCCEx_GetPeriphCLKFreq(RCC_PERIPHCLK_USART1);
  g_demo_diag.usart2_clk_hz = HAL_RCCEx_GetPeriphCLKFreq(RCC_PERIPHCLK_USART2);
  g_demo_diag.sai1_clk_hz = HAL_RCCEx_GetPeriphCLKFreq(RCC_PERIPHCLK_SAI1);
  g_demo_diag.sai2_clk_hz = HAL_RCCEx_GetPeriphCLKFreq(RCC_PERIPHCLK_SAI2);
  if (g_demo_diag.sysclk_hz < 47900000U || g_demo_diag.sysclk_hz > 48100000U) w |= 0x01U;
  if (g_demo_diag.hclk_hz < 47900000U || g_demo_diag.hclk_hz > 48100000U) w |= 0x02U;
  if (g_demo_diag.sai1_clk_hz < 24400000U || g_demo_diag.sai1_clk_hz > 24800000U) w |= 0x04U;
  if (g_demo_diag.sai2_clk_hz < 24400000U || g_demo_diag.sai2_clk_hz > 24800000U) w |= 0x08U;
  if (g_demo_diag.usart1_clk_hz < 47900000U || g_demo_diag.usart1_clk_hz > 48100000U) w |= 0x10U;
  if (g_demo_diag.usart2_clk_hz < 47900000U || g_demo_diag.usart2_clk_hz > 48100000U) w |= 0x20U;
  g_demo_diag.clock_warning_flags = w;
}

static void print_clock(void)
{
  refresh_clocks();
  console_write("CLOCK sys="); console_u32(g_demo_diag.sysclk_hz);
  console_write(" hclk="); console_u32(g_demo_diag.hclk_hz);
  console_write(" pclk1="); console_u32(g_demo_diag.pclk1_hz);
  console_write(" pclk2="); console_u32(g_demo_diag.pclk2_hz);
  console_write(" uart1="); console_u32(g_demo_diag.usart1_clk_hz);
  console_write(" uart2="); console_u32(g_demo_diag.usart2_clk_hz);
  console_write(" sai1="); console_u32(g_demo_diag.sai1_clk_hz);
  console_write(" sai2="); console_u32(g_demo_diag.sai2_clk_hz);
  console_write(" warn="); console_hex8(g_demo_diag.clock_warning_flags);
  console_write("\r\n");
}

static void print_status(void)
{
  console_write("STAT t="); console_u32(g_demo_diag.uptime_ms);
  console_write(" stage="); console_hex8(g_demo_diag.stage);
  console_write(" err="); console_u32(g_demo_diag.error_code);
  console_write(" mode=M"); console_u32(g_demo_diag.mode);
  console_write(" src="); console_write(source_name(g_demo_diag.active_source));
  console_write(" fault="); console_u32(g_demo_diag.fault);
  console_write(" mute="); console_u32(g_demo_diag.muted);
  console_write(" rt[ctl="); console_u32(g_demo_diag.control_task_loops);
  console_write(" aud="); console_u32(g_demo_diag.audio_task_loops);
  console_write(" bt="); console_u32(g_demo_diag.bt_task_loops);
  console_write(" bg="); console_u32(g_demo_diag.background_task_loops);
  console_write("] bt[rms="); console_u32(g_demo_diag.bt_rms);
  console_write(" pp="); console_u32(g_demo_diag.bt_peak_to_peak);
  console_write(" L="); console_u32(g_demo_diag.bt_left_rms);
  console_write(" R="); console_u32(g_demo_diag.bt_right_rms);
  console_write(" mono="); console_u32(g_demo_diag.bt_mono_selected);
  console_write(" present="); console_u32(g_demo_diag.bt_audio_present);
  console_write(" blocks="); console_u32(g_demo_diag.bt_blocks_processed);
  console_write("] tc[rms="); console_u32(g_demo_diag.tc_rms);
  console_write(" pp="); console_u32(g_demo_diag.tc_peak_to_peak);
  console_write(" valid="); console_u32(g_demo_diag.tc_valid);
  console_write(" blocks="); console_u32(g_demo_diag.tc_blocks_processed);
  console_write("] dma="); console_write(g_demo_diag.audio_dma_running ? "RUN" : "OFF");
  console_write(" sw="); console_u32(g_demo_diag.source_switches);
  console_write(" miss="); console_u32(g_demo_diag.service_deadline_misses);
  console_write(" load="); console_u32(g_demo_control.background_load_enabled);

  console_write(" btU[rx="); console_u32(g_demo_diag.bt_uart_rx_bytes);
  console_write(" tx="); console_u32(g_demo_diag.bt_uart_tx_bytes);
  console_write(" lines="); console_u32(g_demo_diag.bt_uart_lines);
  console_write("] rx="); console_u32(g_demo_diag.console_rx_bytes);
  console_write(" rxov="); console_u32(g_demo_diag.console_rx_overruns);
  console_write(" uerr="); console_u32(g_demo_diag.console_uart_errors);
  console_write(" out[tx="); console_u32(g_demo_diag.speaker_dma_running);
  console_write(" blk="); console_u32(g_demo_diag.speaker_blocks_output);
  console_write(" under="); console_u32(g_demo_diag.speaker_underruns);
  console_write(" qov="); console_u32(g_demo_diag.speaker_queue_overruns);
  console_write("] dsp[on="); console_u32(g_demo_control.dsp_enabled);
  console_write(" in="); console_u32(g_demo_diag.dsp_input_rms);
  console_write(" out="); console_u32(g_demo_diag.dsp_output_rms);
  console_write(" us="); console_u32(g_demo_diag.dsp_last_us);
  console_write("] rate["); console_u32(g_demo_diag.control_task_rate_hz);
  console_write("/"); console_u32(g_demo_diag.audio_task_rate_hz);
  console_write("/"); console_u32(g_demo_diag.bt_task_rate_hz);
  console_write("/"); console_u32(g_demo_diag.background_task_rate_hz);
  console_write("]\r\n");
}

static void print_help(void)
{
  console_write(
    "Commands:\r\n"
    "  PING | STATUS | CLOCK | HELP\r\n"
    "  TEST ALL | TEST CLOCK | TEST RTOS | TEST DSP | TEST AUDIO | TEST BT\r\n"
    "  MODE AUTO | MODE BT | MODE TC | MODE OFF\r\n"
    "  BT ON | BT OFF | TC ON | TC OFF | ALL ON | ALL OFF\r\n"
    "  BTLINK AUTO | BTLINK ON | BTLINK OFF\r\n"
    "  AUDIO START | AUDIO STOP | AUDIO STATUS\r\n"
    "  OUTPUT ON | OUTPUT OFF | OUTPUT LEVEL SAFE|LOW|MED|HIGH|UNITY | TEST OUTPUT\r\n"
    "  DIRECT BT|TC LEFT|RIGHT|MIX | DIRECT OFF | DIRECT STATUS\r\n"
    "  INTEGRITY STATUS | INTEGRITY RESET\r\n"
    "  TONE 1K | TONE 2K | TONE 3K | TONE OFF  (direct SAI2B hardware test)\r\n"
    "  CAPTURE BT | CAPTURE TC | CAPTURE STATUS | CAPTURE DUMP | CAPTURE CANCEL (raw 32-bit words)\r\n"
    "  RECORD BT|TC LEFT|RIGHT|MIX | RECORD STATUS | RECORD DUMP | RECORD CANCEL (pre-DSP PCM)\r\n"
    "  PCM LOW24 | PCM UPPER16 | PCM MIDDLE16 | PCM LOW16 | PCM STATUS\r\n"
    "  BT MONO AUTO | BT MONO MIX | BT MONO LEFT | BT MONO RIGHT\r\n"
    "  DSP ON | DSP OFF | DSP RESET | DSP STATUS\r\n"
    "  DSP EQ ON | DSP EQ OFF | DSP COMP ON | DSP COMP OFF\r\n"
    "  EVIDENCE ON | EVIDENCE OFF | PLOT ON | PLOT OFF\r\n"
    "  !FA real AUTO | !FK real BT-idle calibration | !FV real source evidence | !RM RTOS metrics | !PV debugger proof | !RA light load | !RS single 60s stress | !RD DUAL 60s stress | !RH priority-7 20s CONTINUOUS SPIN (control unresponsive until stop) | !RX load off\r\n"
    "  !GON !GOFF !G0..G3 profiles: neutral/voice/music/wind | !GA auto-level | !GM manual | !GP status\r\n"
    "  NUCLEO B1 PC13: short=next profile, long=toggle auto-level (Nucleo target only)\r\n"
    "  BT PROFILE | BT QUERY\r\n"
    "  LOAD ON | LOAD OFF | !RS CPU STRESS | STREAM ON | STREAM OFF | RESETDIAG\r\n");
}

static uint32_t isqrt_u64(uint64_t x)
{
  uint64_t op = x;
  uint64_t res = 0U;
  uint64_t one = (uint64_t)1U << 62;
  while (one > op) one >>= 2;
  while (one != 0U) {
    if (op >= res + one) {
      op -= res + one;
      res = (res >> 1) + one;
    } else {
      res >>= 1;
    }
    one >>= 2;
  }
  return (uint32_t)res;
}

static void metrics_s16(const int16_t *x, size_t n, uint32_t *rms, uint32_t *pp, uint32_t *peak)
{
  int32_t minv = 32767, maxv = -32768;
  uint32_t p = 0U;
  uint64_t sum_sq = 0U;
  for (size_t i = 0U; i < n; ++i) {
    int32_t v = x[i];
    uint32_t a = uabs32(v);
    if (v < minv) minv = v;
    if (v > maxv) maxv = v;
    if (a > p) p = a;
    sum_sq += (uint64_t)a * (uint64_t)a;
  }
  if (rms) *rms = n ? isqrt_u64(sum_sq / n) : 0U;
  if (pp) *pp = n ? (uint32_t)(maxv - minv) : 0U;
  if (peak) *peak = p;
}

static void update_bt_presence(uint32_t rms, uint32_t pp)
{
  if (g_r28_cal_state == 1U) {
    if (rms > g_r28_cal_max_rms) g_r28_cal_max_rms = rms;
    if (pp > g_r28_cal_max_pp) g_r28_cal_max_pp = pp;
    if (++g_r28_cal_blocks >= 192U) { /* 512 ms of REAL 48-kHz SAI1 PCM */
      if (g_r28_cal_max_rms > 8000U || g_r28_cal_max_pp > 16000U) {
        g_r28_cal_state = 3U; /* no quiet baseline: DO NOT fake absence */
      } else {
        uint32_t r = g_r28_cal_max_rms * 2U + 80U;
        uint32_t p = g_r28_cal_max_pp * 2U + 250U;
        g_demo_control.bt_present_rms_threshold = r > 350U ? r : 350U;
        g_demo_control.bt_present_pp_threshold = p > 900U ? p : 900U;
        g_bt_present_streak = g_bt_absent_streak = 0U;
        g_demo_diag.bt_audio_present = 0U;
        g_r28_cal_state = 2U;
      }
    }
    /* During baseline collection, do not qualify this audio as Bluetooth. */
    return;
  }
  bool strong = (rms >= g_demo_control.bt_present_rms_threshold) || (pp >= g_demo_control.bt_present_pp_threshold);
  if (strong) {
    if (g_bt_present_streak < 255U) g_bt_present_streak++;
    g_bt_absent_streak = 0U;
    if (g_bt_present_streak >= 24U) g_demo_diag.bt_audio_present = 1U; /* ~64 ms stable audio */
  } else {
    if (g_bt_absent_streak < 255U) g_bt_absent_streak++;
    g_bt_present_streak = 0U;
    if (g_bt_absent_streak >= 75U) g_demo_diag.bt_audio_present = 0U; /* ~200 ms dropout hold */
  }
}

static bool bt_connected_now(void)
{
  if (g_demo_control.bt_link_mode == DEMO_BTLINK_FORCE_OFF) return false;
  if (g_demo_control.bt_link_mode == DEMO_BTLINK_FORCE_ON) return true;
  return g_demo_diag.bt_audio_present != 0U;
}

static void sync_status_from_service(void)
{
  ConnectivityOutputs_t o;
  uint32_t old_source = g_demo_diag.active_source;
  if (ConnectivityService_GetStatus(&g_service, &o) == CONNECTIVITY_SERVICE_OK) {
    g_demo_diag.mode = (uint32_t)o.mode;
    g_demo_diag.active_source = (uint32_t)o.active_source;
    g_demo_diag.fault = (uint32_t)o.fault;
    g_demo_diag.muted = o.muted ? 1U : 0U;
    if (g_demo_diag.active_source != old_source) {
      output_queue_reset();
      g_plot_accum_count = 0U;
      g_last_output_source = g_demo_diag.active_source;
      (void)AudioDynamics_Reset(&g_dynamics);
    }
  }
  g_demo_diag.controller_updates = g_service.diagnostics.controller_updates;
  g_demo_diag.source_switches = g_service.diagnostics.source_switches;
  g_demo_diag.faults_entered = g_service.diagnostics.faults_entered;
  g_demo_diag.recoveries = g_service.diagnostics.recoveries;
  g_demo_diag.service_deadline_misses = g_service.diagnostics.service_deadline_misses;
  g_demo_diag.max_service_interval_ms = g_service.diagnostics.max_service_interval_ms;
  g_demo_diag.tc_rms = g_service.last_telecoil_metrics.rms;
  g_demo_diag.tc_peak_to_peak = g_service.last_telecoil_metrics.peak_to_peak;
  g_demo_diag.tc_valid = g_service.last_telecoil_metrics.valid_signal ? 1U : 0U;
}

static void step_service(uint32_t now)
{
  ConnectivityLinkState_t link = {0};
  link.now_ms = now;
  link.interfaces_ready = g_demo_control.interfaces_ready != 0U;
  link.bluetooth_connected = bt_connected_now();
  link.paired_device_available = link.bluetooth_connected;
  (void)ConnectivityService_Step(&g_service, &link);
  sync_status_from_service();
}

static void apply_pending_command(uint32_t now)
{
  uint32_t p = g_pending_command_plus1;
  if (p != 0U) {
    g_pending_command_plus1 = 0U;
    (void)ConnectivityService_ApplyCommand(&g_service, (ConnectivityCommand_t)(p - 1U), now);
    sync_status_from_service();
  }
}

static void apply_requested_mode(uint32_t now)
{
  uint32_t m = g_demo_control.requested_source_mode;
  ConnectivityCommand_t cmd = CONNECTIVITY_COMMAND_ENABLE_BOTH;
  if (m == g_last_requested_mode) return;
  if (m == DEMO_MODE_BT_ONLY) cmd = CONNECTIVITY_COMMAND_BLUETOOTH_ONLY;
  else if (m == DEMO_MODE_TC_ONLY) cmd = CONNECTIVITY_COMMAND_TELECOIL_ONLY;
  else if (m == DEMO_MODE_OFF) cmd = CONNECTIVITY_COMMAND_DISABLE_BOTH;
  else { m = DEMO_MODE_AUTO; cmd = CONNECTIVITY_COMMAND_ENABLE_BOTH; }
  (void)ConnectivityService_ApplyCommand(&g_service, cmd, now);
  g_last_requested_mode = m;
  sync_status_from_service();
}

static void process_bt_pcm(const int16_t *mono, uint32_t now)
{
  uint32_t rms, pp;
  metrics_s16(mono, DEMO_FRAMES_PER_HALF, &rms, &pp, NULL);
  g_demo_diag.bt_rms = rms;
  g_demo_diag.bt_peak_to_peak = pp;
  update_bt_presence(rms, pp);
  step_service(now);
  memcpy(g_r27_pre_source,mono,sizeof(g_r27_pre_source));
  (void)ConnectivityService_ProcessBluetoothBlock(&g_service, mono, g_routed,
                                                   DEMO_FRAMES_PER_HALF, now, now);
  if (g_demo_diag.active_source == 2U) {
    process_selected_audio(g_routed, 2U);
  }
  g_demo_diag.bt_blocks_processed++;
}

static void process_tc_pcm(const int16_t *mono, uint32_t now)
{
  memcpy(g_r27_pre_source,mono,sizeof(g_r27_pre_source));
  (void)ConnectivityService_ProcessTelecoilBlock(&g_service, mono, g_routed,
                                                  DEMO_FRAMES_PER_HALF, now);
  sync_status_from_service();
  if (g_demo_diag.active_source == 1U) {
    if (g_r27_tc_clean_enabled) {
      TcCleanup_Block(&g_r27_tc_filter,g_routed,g_r27_tc_clean,DEMO_FRAMES_PER_HALF);
      process_selected_audio(g_r27_tc_clean, 1U);
    } else process_selected_audio(g_routed, 1U);
  }
  g_demo_diag.tc_blocks_processed++;
}

static void process_bt_words(const uint32_t *words, uint32_t now)
{
  uint32_t lrms = 0U, rrms = 0U;
  uint32_t mode;
  raw_capture_append(RAW_CAPTURE_SOURCE_BT, words, DEMO_FRAMES_PER_HALF);
  if (PcmTransport_DecodeStereoWordsMode(words, DEMO_FRAMES_PER_HALF, g_left, g_right,
                                         (PcmTransportDecodeMode_t)g_demo_control.pcm_decode_mode) != PCM_TRANSPORT_OK) return;

  listen_capture_append(RAW_CAPTURE_SOURCE_BT, g_left, g_right, DEMO_FRAMES_PER_HALF);
  metrics_s16(g_left, DEMO_FRAMES_PER_HALF, &lrms, NULL, NULL);
  metrics_s16(g_right, DEMO_FRAMES_PER_HALF, &rrms, NULL, NULL);
  g_demo_diag.bt_left_rms = lrms;
  g_demo_diag.bt_right_rms = rrms;

  if (g_demo_control.direct_source != DEMO_DIRECT_OFF) {
    if (g_demo_control.direct_source == DEMO_DIRECT_BT) {
      direct_process_block(DEMO_DIRECT_BT, g_left, g_right, lrms, rrms);
      g_demo_diag.bt_rms = (g_demo_control.direct_channel == DEMO_CHANNEL_LEFT) ? lrms :
                           (g_demo_control.direct_channel == DEMO_CHANNEL_RIGHT) ? rrms : ((lrms + rrms) / 2U);
    }
    g_demo_diag.bt_blocks_processed++;
    return;
  }

  mode = g_demo_control.bt_mono_mode;
  if (mode == DEMO_BT_MONO_AUTO) {
    if (lrms > (rrms * 2U + 64U)) mode = DEMO_BT_MONO_LEFT;
    else if (rrms > (lrms * 2U + 64U)) mode = DEMO_BT_MONO_RIGHT;
    else mode = DEMO_BT_MONO_MIX;
  }
  g_demo_diag.bt_mono_selected = mode;

  for (size_t i = 0U; i < DEMO_FRAMES_PER_HALF; ++i) {
    if (mode == DEMO_BT_MONO_LEFT) g_mono[i] = g_left[i];
    else if (mode == DEMO_BT_MONO_RIGHT) g_mono[i] = g_right[i];
    else g_mono[i] = Audio_MixStereoToMono(g_left[i], g_right[i]);
  }
  process_bt_pcm(g_mono, now);
}

static void process_tc_words(const uint32_t *words, uint32_t now)
{
  uint32_t lrms = 0U, rrms = 0U;
  raw_capture_append(RAW_CAPTURE_SOURCE_TC, words, DEMO_FRAMES_PER_HALF);
  if (PcmTransport_DecodeStereoWordsMode(words, DEMO_FRAMES_PER_HALF, g_left, g_right,
                                         (PcmTransportDecodeMode_t)g_demo_control.pcm_decode_mode) != PCM_TRANSPORT_OK) return;
  listen_capture_append(RAW_CAPTURE_SOURCE_TC, g_left, g_right, DEMO_FRAMES_PER_HALF);
  metrics_s16(g_left, DEMO_FRAMES_PER_HALF, &lrms, NULL, NULL);
  metrics_s16(g_right, DEMO_FRAMES_PER_HALF, &rrms, NULL, NULL);

  if (g_demo_control.direct_source != DEMO_DIRECT_OFF) {
    if (g_demo_control.direct_source == DEMO_DIRECT_TC) {
      direct_process_block(DEMO_DIRECT_TC, g_left, g_right, lrms, rrms);
      g_demo_diag.tc_rms = (g_demo_control.direct_channel == DEMO_CHANNEL_LEFT) ? lrms :
                           (g_demo_control.direct_channel == DEMO_CHANNEL_RIGHT) ? rrms : ((lrms + rrms) / 2U);
    }
    g_demo_diag.tc_blocks_processed++;
    return;
  }

  for (size_t i = 0U; i < DEMO_FRAMES_PER_HALF; ++i) g_mono[i] = Audio_MixStereoToMono(g_left[i], g_right[i]);
  process_tc_pcm(g_mono, now);
}

static HAL_StatusTypeDef start_dma(void)
{
  HAL_StatusTypeDef a = HAL_OK, b = HAL_OK, c = HAL_OK;
  const bool tone_only = (g_demo_control.test_tone_step != 0U);
  /* SAI1A is the physical master timing source shared by both PCM1808s.
   * Keep its RX DMA running for telecoil-only capture: otherwise the SAI1
   * clock generator stops and SAI2A synchronous slave cannot receive. */
  const bool need_bt = !tone_only;
  const bool need_tc = (!tone_only && g_demo_control.direct_source != DEMO_DIRECT_BT);

  if (g_dma_started) return HAL_OK;
  memset(g_bt_dma_words, 0, sizeof(g_bt_dma_words));
  memset(g_tc_dma_words, 0, sizeof(g_tc_dma_words));
  memset(g_speaker_tx_dma, 0, sizeof(g_speaker_tx_dma));
  output_queue_reset();
  g_bt_cap_head = g_bt_cap_tail = 0U;
  g_tc_cap_head = g_tc_cap_tail = 0U;
  g_r262_hd_valid = 0U; /* never stitch PCM across separate DMA sessions */
  g_r262_hd_source = 0U;
  g_plot_accum_count = 0U;
  g_demo_diag.bt_capture_depth = 0U;
  g_demo_diag.tc_capture_depth = 0U;
  g_bt_dma_started = g_tc_dma_started = g_speaker_dma_started = 0U;

  /* Speaker starts with silence and does not consume real audio until the
   * elastic FIFO is primed. */
  c = SpeakerOutput_StartDma(g_speaker_tx_dma, DEMO_OUTPUT_DMA_SAMPLES);
  if (c == HAL_OK) {
    g_speaker_dma_started = 1U;
    g_demo_diag.speaker_dma_running = 1U;
  }

  /* DIRECT reference mode intentionally starts only the selected receiver.
   * Normal/demo mode still starts both inputs. This removes unnecessary DMA
   * interrupts while judging raw audio quality. */
  if (need_bt) {
    a = HAL_SAI_Receive_DMA(&hsai_BlockA1, (uint8_t *)g_bt_dma_words, DEMO_DMA_WORDS);
    if (a == HAL_OK) g_bt_dma_started = 1U;
  }
  if (need_tc) {
    b = HAL_SAI_Receive_DMA(&hsai_BlockA2, (uint8_t *)g_tc_dma_words, DEMO_DMA_WORDS);
    if (b == HAL_OK) g_tc_dma_started = 1U;
  }

  if (a == HAL_OK && b == HAL_OK && c == HAL_OK) {
    g_dma_started = 1U;
    g_demo_diag.audio_dma_running = (g_bt_dma_started || g_tc_dma_started) ? 1U : 0U;
    return HAL_OK;
  }

  if (g_bt_dma_started) (void)HAL_SAI_DMAStop(&hsai_BlockA1);
  if (g_tc_dma_started) (void)HAL_SAI_DMAStop(&hsai_BlockA2);
  if (g_speaker_dma_started) (void)SpeakerOutput_StopDma();
  g_bt_dma_started = g_tc_dma_started = g_speaker_dma_started = 0U;
  g_demo_diag.audio_dma_running = 0U;
  g_demo_diag.speaker_dma_running = 0U;
  return (a != HAL_OK) ? a : ((b != HAL_OK) ? b : c);
}

static void stop_dma(void)
{
  if (g_bt_dma_started) (void)HAL_SAI_DMAStop(&hsai_BlockA1);
  if (g_tc_dma_started) (void)HAL_SAI_DMAStop(&hsai_BlockA2);
  if (g_speaker_dma_started) (void)SpeakerOutput_StopDma();
  output_queue_reset();
  memset(g_speaker_tx_dma, 0, sizeof(g_speaker_tx_dma));
  g_bt_dma_started = g_tc_dma_started = g_speaker_dma_started = 0U;
  g_dma_started = 0U;
  g_r262_hd_valid = 0U;
  g_demo_diag.audio_dma_running = 0U;
  g_demo_diag.speaker_dma_running = 0U;
}

static bool bt_tx(const char *cmd, size_t len, void *ctx)
{
  (void)ctx;
  if (HAL_UART_Transmit(&huart1, (uint8_t *)cmd, (uint16_t)len, 250U) == HAL_OK) {
    g_demo_diag.bt_uart_tx_bytes += (uint32_t)len;
    return true;
  }
  return false;
}

static void bt_delay(uint32_t ms, void *ctx)
{
  (void)ctx;
  vTaskDelay(pdMS_TO_TICKS(ms));
}

static void bt_query(void)
{
  static const char *const q[] = {"AT+QM\r\n", "AT+QA\r\n", "AT+TS\r\n", "AT+TL\r\n"};
  for (size_t i = 0U; i < sizeof(q)/sizeof(q[0]); ++i) {
    uint16_t n = (uint16_t)strlen(q[i]);
    if (HAL_UART_Transmit(&huart1, (uint8_t *)q[i], n, 250U) == HAL_OK) g_demo_diag.bt_uart_tx_bytes += n;
    vTaskDelay(pdMS_TO_TICKS(100U));
  }
}

static void poll_bt_uart(void)
{
  uint8_t c;
  while (bt_rx_pop(&c)) {
    if (c == '\r' || c == '\n') {
      if (g_bt_line_len > 0U) {
        g_bt_line[g_bt_line_len] = '\0';
        g_bt_line_len = 0U;
        g_demo_diag.bt_uart_lines++;
      }
    } else if (c >= 0x20U && c <= 0x7EU && g_bt_line_len < BT_LINE_LEN - 1U) {
      g_bt_line[g_bt_line_len++] = (char)c;
    }
  }
}

static uint32_t test_clock(void)
{
  refresh_clocks();
  return g_demo_diag.clock_warning_flags == 0U ? 1U : 0U;
}

static uint32_t test_rtos(void)
{
  return (g_demo_diag.control_task_loops > 10U && g_demo_diag.audio_task_loops > 10U &&
          g_demo_diag.bt_task_loops > 10U && g_demo_diag.background_task_loops > 10U) ? 1U : 0U;
}

static uint32_t test_dsp(void)
{
  if (!(g_demo_diag.controller_updates > 10U && g_demo_diag.mode >= 1U && g_demo_diag.mode <= 4U)) return 0U;
  if (g_demo_diag.dsp_blocks == 0U) return 2U;
  return (g_demo_diag.dsp_max_us < 2667U) ? 1U : 0U;
}

static uint32_t test_audio(void)
{
  if (!g_demo_diag.audio_dma_running) return 2U; /* skipped/not started */
  return ((g_demo_diag.bt_half_callbacks + g_demo_diag.bt_full_callbacks +
           g_demo_diag.tc_half_callbacks + g_demo_diag.tc_full_callbacks) > 2U &&
          g_demo_diag.sai1_errors == 0U && g_demo_diag.sai2_errors == 0U) ? 1U : 0U;
}

static uint32_t test_output(void)
{
  if (!g_demo_diag.speaker_dma_running) return 2U;
  return ((g_demo_diag.speaker_half_callbacks + g_demo_diag.speaker_full_callbacks) > 2U &&
          g_demo_diag.sai2_errors == 0U) ? 1U : 0U;
}

static uint32_t test_bt(void)
{
  if (g_demo_diag.bt_uart_tx_bytes == 0U && g_demo_diag.bt_uart_rx_bytes == 0U) return 2U;
  return 1U;
}

static void print_test_result(const char *name, uint32_t r)
{
  console_write("TEST "); console_write(name); console_write(" ");
  if (r == 1U) console_write("PASS\r\n");
  else if (r == 2U) console_write("SKIP/NOT ACTIVE\r\n");
  else console_write("FAIL\r\n");
}

static void test_all(void)
{
  uint32_t c, r, d, a, b, o, mask = 0U;
  g_demo_diag.selftest_runs++;
  c = test_clock(); r = test_rtos(); d = test_dsp(); a = test_audio(); b = test_bt(); o = test_output();
  print_test_result("CLOCK", c);
  print_test_result("RTOS", r);
  print_test_result("DSP", d);
  print_test_result("AUDIO", a);
  print_test_result("OUTPUT", o);
  print_test_result("BT401", b);
  if (c != 1U) mask |= 0x01U;
  if (r != 1U) mask |= 0x02U;
  if (d == 0U) mask |= 0x04U;
  if (a == 0U) mask |= 0x08U;
  if (b == 0U) mask |= 0x10U;
  if (o == 0U) mask |= 0x20U;
  g_demo_diag.last_selftest_mask = mask;
  if (mask != 0U) g_demo_diag.selftest_failures++;
  console_write("TEST ALL mask="); console_hex8(mask);
  console_write(mask == 0U ? " CORE PASS\r\n" : " CHECK ITEMS ABOVE\r\n");
}

static void queue_protocol_command(const char *line)
{
  ConnectivityCommand_t cmd;
  ControlProtocolResult_t r = ControlProtocol_Parse(line, strlen(line), &cmd);
  if (r == CONTROL_PROTOCOL_STATUS_REQUEST) {
    print_status();
    g_demo_diag.console_commands_ok++;
  } else if (r == CONTROL_PROTOCOL_OK) {
    g_pending_command_plus1 = (uint32_t)cmd + 1U;
    g_demo_diag.console_commands_ok++;
    console_write("OK queued production control command\r\n");
  } else {
    g_demo_diag.console_commands_error++;
    console_write("ERR unknown command; type HELP\r\n");
  }
}

static const char *r21_name(uint32_t pass)
{
  return pass ? "PASS" : "FAIL";
}

static void r21_reset_audio_counters(void)
{
  taskENTER_CRITICAL();
  g_demo_diag.bt_half_callbacks = g_demo_diag.bt_full_callbacks = 0U;
  g_demo_diag.tc_half_callbacks = g_demo_diag.tc_full_callbacks = 0U;
  g_demo_diag.bt_dma_overruns = g_demo_diag.tc_dma_overruns = 0U;
  g_demo_diag.sai1_errors = g_demo_diag.sai2_errors = 0U;
  g_demo_diag.bt_blocks_processed = g_demo_diag.tc_blocks_processed = 0U;
  g_demo_diag.bt_rms = g_demo_diag.bt_left_rms = g_demo_diag.bt_right_rms = 0U;
  g_demo_diag.tc_rms = 0U;
  g_demo_diag.speaker_half_callbacks = g_demo_diag.speaker_full_callbacks = 0U;
  g_demo_diag.speaker_blocks_output = 0U;
  g_demo_diag.speaker_underruns = g_demo_diag.speaker_queue_overruns = 0U;
  g_r21_conceal_total = 0U;
  g_plot_accum_count = 0U;
  g_plot_snapshot_seq = 0U;
  taskEXIT_CRITICAL();
}

static void r21_start_source(uint32_t source, uint8_t auto_level)
{
  g_demo_control.stream_enabled = 0U;
  g_demo_control.evidence_enabled = 0U;
  g_demo_control.background_load_enabled = 0U;
  g_r31_dual_load = 0U;
  g_demo_control.dsp_enabled = 0U;
  g_demo_control.pcm_decode_mode = PCM_DECODE_LOW24;
  g_demo_control.direct_source = source;
  g_demo_control.direct_channel = DEMO_CHANNEL_LEFT;
  /* R25: keep the legacy binary plot stream permanently OFF.  Live
   * scope snapshots are collected internally and requested explicitly as
   * ASCII packets with !SP.  This prevents 0xA5 0x5A "HD" binary frames
   * from contaminating the human-readable console. */
  g_demo_control.plot_enabled = 0U;
  g_demo_control.audio_capture_enabled = 1U;
  g_demo_control.output_enabled = 1U;
  g_demo_control.test_tone_step = 0U;
  g_r21_smart_mono_enabled = (source == DEMO_DIRECT_BT && auto_level) ? 1U : 0U;
  g_r21_auto_level_enabled = auto_level;
  g_r21_auto_gain_q15 = auto_level ? 32768U : 8192U;
  g_r24_gain_update_div = 0U;
  g_r26_left_weight_q15 = 16384U;
  g_r26_target_left_weight_q15 = 16384U;
  TcCleanup_Reset(&g_r27_tc_filter);
  g_demo_control.output_gain_q15 = g_r21_auto_gain_q15;
  if (g_dma_started) stop_dma();
  output_queue_reset();
  r21_reset_audio_counters();
}

static void r21_health(void)
{
  refresh_clocks();
  uint32_t ok = (test_clock() && test_rtos() && g_demo_diag.console_rx_irq_armed &&
                 g_demo_diag.error_code == 0U);
  (void)snprintf(g_r21_line, sizeof(g_r21_line),
    "R21|HEALTH|%s|clk=%lu|ctl=%lu|aud=%lu|bt=%lu|bg=%lu|rxov=%lu|uerr=%lu\r\n",
    r21_name(ok), (unsigned long)g_demo_diag.sysclk_hz,
    (unsigned long)g_demo_diag.control_task_loops,
    (unsigned long)g_demo_diag.audio_task_loops,
    (unsigned long)g_demo_diag.bt_task_loops,
    (unsigned long)g_demo_diag.background_task_loops,
    (unsigned long)g_demo_diag.console_rx_overruns,
    (unsigned long)g_demo_diag.console_uart_errors);
  console_write(g_r21_line);
}

static void r21_output_start(void)
{
  /* ACK first.  Starting/restarting speaker DMA must never prevent the host
   * from learning that the command was accepted.  OUT_CHECK proves the
   * physical DMA state one second later. */
  console_write("R21|OUT_START|PASS|tone=1000|gain=50|ack=PRESTART\r\n");
  g_r21_auto_level_enabled = 0U;
  g_r21_smart_mono_enabled = 0U;
  g_demo_control.direct_source = DEMO_DIRECT_BT;
  g_demo_control.direct_channel = DEMO_CHANNEL_LEFT;
  g_demo_control.plot_enabled = 0U;
  g_demo_control.audio_capture_enabled = 1U;
  g_demo_control.output_enabled = 1U;
  g_demo_control.output_gain_q15 = 16384U;
  g_demo_control.test_tone_step = 1U;
  g_demo_diag.test_tone_hz = 1000U;
  g_tone_phase = 0U;
  if (g_dma_started) stop_dma();
  output_queue_reset();
  r21_reset_audio_counters();
}

static void r21_output_check(void)
{
  uint32_t spk = g_demo_diag.speaker_half_callbacks + g_demo_diag.speaker_full_callbacks;
  uint32_t ok = (g_demo_diag.speaker_dma_running && spk >= 8U && g_demo_diag.test_tone_hz == 1000U);
  (void)snprintf(g_r21_line, sizeof(g_r21_line),
    "R21|OUT_CHECK|%s|spk=%lu|blocks=%lu|tone=%lu|qov=%lu\r\n",
    r21_name(ok), (unsigned long)spk,
    (unsigned long)g_demo_diag.speaker_blocks_output,
    (unsigned long)g_demo_diag.test_tone_hz,
    (unsigned long)g_demo_diag.speaker_queue_overruns);
  console_write(g_r21_line);
}

static void r21_output_stop(void)
{
  g_demo_control.test_tone_step = 0U;
  g_demo_diag.test_tone_hz = 0U;
  output_queue_reset();
  console_write("R21|OUT_STOP|PASS\r\n");
}

static void r21_bt_check(void)
{
  uint32_t cb = g_demo_diag.bt_half_callbacks + g_demo_diag.bt_full_callbacks;
  uint32_t spk = g_demo_diag.speaker_half_callbacks + g_demo_diag.speaker_full_callbacks;
  uint32_t input_ok = (cb >= 8U && g_demo_diag.bt_blocks_processed >= 4U && g_demo_diag.sai1_errors == 0U);
  uint32_t signal_ok = (g_demo_diag.bt_rms >= 24U || g_demo_diag.bt_left_rms >= 24U || g_demo_diag.bt_right_rms >= 24U);
  uint32_t output_ok = (g_demo_diag.speaker_dma_running && spk >= 8U && g_demo_diag.speaker_queue_overruns == 0U);
  uint32_t ok = input_ok && signal_ok && output_ok;
  (void)snprintf(g_r21_line, sizeof(g_r21_line),
    "R21|BT|%s|cb=%lu|blk=%lu|rms=%lu|L=%lu|R=%lu|spk=%lu|und=%lu|con=%lu|gain=%lu|capov=%lu|capd=%lu|capmax=%lu|mixL=%lu|mixR=%lu\r\n",
    r21_name(ok), (unsigned long)cb, (unsigned long)g_demo_diag.bt_blocks_processed,
    (unsigned long)g_demo_diag.bt_rms, (unsigned long)g_demo_diag.bt_left_rms,
    (unsigned long)g_demo_diag.bt_right_rms, (unsigned long)spk,
    (unsigned long)g_demo_diag.speaker_underruns, (unsigned long)g_r21_conceal_total,
    (unsigned long)g_demo_control.output_gain_q15,
    (unsigned long)g_demo_diag.bt_dma_overruns, (unsigned long)g_demo_diag.bt_capture_depth,
    (unsigned long)g_demo_diag.bt_capture_max_depth,
    (unsigned long)g_r26_left_weight_q15, (unsigned long)(32768U - g_r26_left_weight_q15));
  console_write(g_r21_line);
}

static void r21_tc_check(void)
{
  uint32_t cb = g_demo_diag.tc_half_callbacks + g_demo_diag.tc_full_callbacks;
  uint32_t spk = g_demo_diag.speaker_half_callbacks + g_demo_diag.speaker_full_callbacks;
  uint32_t transport = (cb >= 8U && g_demo_diag.tc_blocks_processed >= 4U && g_demo_diag.sai2_errors == 0U);
  uint32_t signal = (g_demo_diag.tc_rms >= 16U);
  uint32_t ok = transport && signal && g_demo_diag.speaker_dma_running && spk >= 8U;
  (void)snprintf(g_r21_line, sizeof(g_r21_line),
    "R21|TC|%s|cb=%lu|blk=%lu|rms=%lu|spk=%lu|saierr=%lu|und=%lu|gain=%lu\r\n",
    r21_name(ok), (unsigned long)cb, (unsigned long)g_demo_diag.tc_blocks_processed,
    (unsigned long)g_demo_diag.tc_rms, (unsigned long)spk,
    (unsigned long)g_demo_diag.sai2_errors, (unsigned long)g_demo_diag.speaker_underruns,
    (unsigned long)g_demo_control.output_gain_q15);
  console_write(g_r21_line);
}

static void r21_scope(void)
{
  /* R26 explicit compact scope packet: one line, one sequence, no legacy
   * binary stream and no multi-line parser state.  256 samples are sent as
   * fixed-width int16 hex for robust framing at 115200 baud. */
  static const char hx[] = "0123456789ABCDEF";
  int16_t copy[DEMO_PLOT_SAMPLES];
  uint32_t seq, src;
  size_t n;
  g_demo_control.plot_enabled = 0U;
  taskENTER_CRITICAL();
  seq = g_plot_snapshot_seq;
  src = g_demo_diag.active_source;
  memcpy(copy, g_plot_proc_snapshot, sizeof(copy));
  taskEXIT_CRITICAL();

  if (seq == 0U) {
    console_write("R26|SCOPE|WAIT|reason=no_snapshot\r\n");
    return;
  }
  n = (size_t)snprintf(g_r21_line, sizeof(g_r21_line),
      "R26|SCOPE|PASS|seq=%lu|src=%lu|fs=%lu|n=%u|hex=",
      (unsigned long)seq, (unsigned long)src,
      (unsigned long)DEMO_PLOT_RATE_HZ, (unsigned)DEMO_PLOT_SAMPLES);
  for (uint32_t i = 0U; i < DEMO_PLOT_SAMPLES && (n + 5U) < sizeof(g_r21_line); ++i) {
    uint16_t v = (uint16_t)copy[i];
    g_r21_line[n++] = hx[(v >> 12) & 0x0FU];
    g_r21_line[n++] = hx[(v >> 8) & 0x0FU];
    g_r21_line[n++] = hx[(v >> 4) & 0x0FU];
    g_r21_line[n++] = hx[v & 0x0FU];
  }
  g_r21_line[n++] = '\r';
  g_r21_line[n++] = '\n';
  g_r21_line[n] = '\0';
  console_write(g_r21_line);
  g_plot_last_sent_seq = seq;
}

/* Explicit high-resolution scope command. The original !SP is untouched.
 * Eight bounded UART records avoid truncation of a giant response. */
static void r262_scope_hd(void)
{
  static const char hx[] = "0123456789ABCDEF";
  static int16_t copy[R262_HD_SAMPLES];
  uint32_t seq, src;
  uint16_t valid;
  taskENTER_CRITICAL();
  seq = g_r262_hd_seq;
  valid = g_r262_hd_valid;
  src = g_r262_hd_source;
  if (valid == R262_HD_SAMPLES) memcpy(copy, g_r262_hd_buffer, sizeof(copy));
  taskEXIT_CRITICAL();
  if (valid != R262_HD_SAMPLES) {
    console_write("R26|SCOPEHD|WAIT|reason=buffer_filling\r\n");
    return;
  }
  for (uint32_t chunk = 0U; chunk < R262_HD_SAMPLES / 256U; ++chunk) {
    size_t n = (size_t)snprintf(g_r21_line, sizeof(g_r21_line),
      "R26|SCOPEHD|D|seq=%lu|src=%lu|fs=12000|n=%u|i=%lu|hex=",
      (unsigned long)seq, (unsigned long)src,
      (unsigned)R262_HD_SAMPLES, (unsigned long)(chunk * 256U));
    for (uint32_t k = 0U; k < 256U; ++k) {
      uint16_t v = (uint16_t)copy[chunk * 256U + k];
      g_r21_line[n++] = hx[(v >> 12) & 15U];
      g_r21_line[n++] = hx[(v >> 8) & 15U];
      g_r21_line[n++] = hx[(v >> 4) & 15U];
      g_r21_line[n++] = hx[v & 15U];
    }
    g_r21_line[n++] = '\r'; g_r21_line[n++] = '\n'; g_r21_line[n] = '\0';
    console_write(g_r21_line);
  }
  (void)snprintf(g_r21_line, sizeof(g_r21_line),
    "R26|SCOPEHD|END|seq=%lu|n=%u\r\n",
    (unsigned long)seq, (unsigned)R262_HD_SAMPLES);
  console_write(g_r21_line);
}

/* R27 low-latency dual-channel monitor, separate from the untouched !SP/!SH.
 * Capture 128 aligned 16-bit samples from each of pre- and post-DSP.
 * Payload base64 carries exact s16 little-endian PCM, never quantized;
 * UART bandwidth sets the upper update-rate limit, not the audio DMA rate. */
static void r27_b64_128(const int16_t *src,char *dst) {
  static const char b64[]="ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
  uint8_t bytes[256U];
  for(uint32_t i=0;i<128U;i++) { uint16_t v=(uint16_t)src[i];bytes[2U*i]=(uint8_t)v;bytes[2U*i+1U]=(uint8_t)(v>>8); }
  uint32_t q=0U;
  for(uint32_t i=0;i<256U;i+=3U) {
    uint32_t rem=256U-i;
    uint32_t v=((uint32_t)bytes[i]<<16)|((rem>1U?(uint32_t)bytes[i+1U]:0U)<<8)|(rem>2U?bytes[i+2U]:0U);
    dst[q++]=b64[(v>>18)&63U];dst[q++]=b64[(v>>12)&63U];
    dst[q++]=(rem>1U)?b64[(v>>6)&63U]:'=';
    dst[q++]=(rem>2U)?b64[v&63U]:'=';
  }
  dst[q]='\0';
}
static void r27_scope_dual(void) {
  int16_t pre[128],post[128];
  static char a[345], b[345];
  uint32_t seq,src;
  taskENTER_CRITICAL();
  seq=g_plot_snapshot_seq;src=g_demo_diag.active_source;
  if(seq>0U) {
    memcpy(pre,g_plot_raw_snapshot+128U,sizeof(pre));
    memcpy(post,g_plot_proc_snapshot+128U,sizeof(post));
  }
  taskEXIT_CRITICAL();
  if(seq==0U || src==0U) {console_write("R27|DUAL|WAIT|reason=inactive_or_filling\r\n");return;}
  r27_b64_128(pre,a);r27_b64_128(post,b);
  uint16_t crc=crc16_update(0xFFFFU,(const uint8_t *)pre,sizeof(pre));
  crc=crc16_update(crc,(const uint8_t *)post,sizeof(post));
  /* A sequence is the snapshot ID, not a stream sample index. The PC must
   * never join separated snapshots as if they were continuous audio. */
  (void)snprintf(g_r21_line,sizeof(g_r21_line),
    "R27|DUAL|PASS|seq=%lu|src=%lu|fs=12000|n=128|pre64=%s|post64=%s|crc=%04X\r\n",
    (unsigned long)seq,(unsigned long)src,a,b,(unsigned)crc);
  console_write(g_r21_line);
}

static void r21_diag(void)
{
  uint32_t btcb = g_demo_diag.bt_half_callbacks + g_demo_diag.bt_full_callbacks;
  uint32_t tccb = g_demo_diag.tc_half_callbacks + g_demo_diag.tc_full_callbacks;
  uint32_t spk = g_demo_diag.speaker_half_callbacks + g_demo_diag.speaker_full_callbacks;
  (void)snprintf(g_r21_line, sizeof(g_r21_line),
    "R21|DIAG1|PASS|err=%lu|clk=%lu|btcb=%lu|btrms=%lu|tccb=%lu|tcrms=%lu|spk=%lu\r\n",
    (unsigned long)g_demo_diag.error_code, (unsigned long)g_demo_diag.sysclk_hz,
    (unsigned long)btcb, (unsigned long)g_demo_diag.bt_rms,
    (unsigned long)tccb, (unsigned long)g_demo_diag.tc_rms, (unsigned long)spk);
  console_write(g_r21_line);
  (void)snprintf(g_r21_line, sizeof(g_r21_line),
    "R21|DIAG2|PASS|und=%lu|con=%lu|qov=%lu|btov=%lu|tcov=%lu|s1e=%lu|s2e=%lu|rxov=%lu|uerr=%lu|src=%lu\r\n",
    (unsigned long)g_demo_diag.speaker_underruns, (unsigned long)g_r21_conceal_total,
    (unsigned long)g_demo_diag.speaker_queue_overruns, (unsigned long)g_demo_diag.bt_dma_overruns,
    (unsigned long)g_demo_diag.tc_dma_overruns, (unsigned long)g_demo_diag.sai1_errors,
    (unsigned long)g_demo_diag.sai2_errors, (unsigned long)g_demo_diag.console_rx_overruns,
    (unsigned long)g_demo_diag.console_uart_errors, (unsigned long)g_demo_diag.active_source);
  console_write(g_r21_line);
}

static void r24_tc_clock_probe(void)
{
  /* R26.2: SAI1A master -> internal SAI1 SYNCOUT -> SAI2A SYNCEN.
   * PB12/PB13 are NOT the clock source for this PCB, so static GPIO levels
   * must never produce a false "clock missing" diagnosis.
   * The actual proof of receiving audio remains SAI2 DMA callbacks. */
  uint32_t sai1_enabled = ((SAI1_Block_A->CR1 & SAI_xCR1_SAIEN) != 0U);
  uint32_t syncout = ((SAI1->GCR & SAI_GCR_SYNCOUT) != 0U);
  uint32_t sai2_sync = ((SAI2_Block_A->CR1 & SAI_xCR1_SYNCEN) == SAI_xCR1_SYNCEN_1);
  uint32_t sai2_enabled = ((SAI2_Block_A->CR1 & SAI_xCR1_SAIEN) != 0U);
  uint32_t btcb = g_demo_diag.bt_half_callbacks + g_demo_diag.bt_full_callbacks;
  uint32_t tccb = g_demo_diag.tc_half_callbacks + g_demo_diag.tc_full_callbacks;
  /* Some STM32 HAL configurations report zero SYNCOUT/SYNCEN although
   * both peripherals are externally driven from the same physical clocks.
   * DMA progression proves framing; it does NOT certify the exact clock topology. */
  uint32_t clock_live = (sai1_enabled && sai2_enabled && btcb>0U && tccb>0U);
  uint32_t ok = clock_live;
  (void)snprintf(g_r21_line, sizeof(g_r21_line),
    "R21|TC_CLOCK|%s|mode=DUAL_SAI_DMA_ACTIVITY|sai1on=%lu|syncout=%lu|sai2sync=%lu|sai2on=%lu|mastercb=%lu|tccb=%lu|fsEdges=NA|sckEdges=NA\r\n",
    r21_name(ok), (unsigned long)sai1_enabled, (unsigned long)syncout,
    (unsigned long)sai2_sync, (unsigned long)sai2_enabled,
    (unsigned long)btcb, (unsigned long)tccb);
  console_write(g_r21_line);
}

/* Group build: physical UI and optional portable DSP controls.
 * The queue is deliberately nonblocking, including from USER button polling. */
static void group_event_send(uint8_t type, uint8_t value)
{
  GroupEvent e = {type, value};
  if (g_group_events == NULL || xQueueSend(g_group_events, &e, 0U) != pdPASS)
    g_group_telemetry.event_drops++;
}
static void group_print_status(void)
{
  (void)snprintf(g_r21_line, sizeof(g_r21_line),
    "GROUP|STATUS|enabled=%lu|auto=%lu|profile=%lu|scene=%lu|gain=%lu|mean=%lu|blocks=%lu|events=%lu|drop=%lu|button=%lu|guard=%lu|guard_events=%lu|recoveries=%lu\r\n",
    (unsigned long)g_group_telemetry.enabled,
    (unsigned long)g_group_telemetry.automatic,
    (unsigned long)g_group_telemetry.profile,
    (unsigned long)g_group_telemetry.scene,
    (unsigned long)g_group_telemetry.gain_q15,
    (unsigned long)g_group_telemetry.input_mean_abs,
    (unsigned long)g_group_telemetry.processed_blocks,
    (unsigned long)g_group_telemetry.event_count,
    (unsigned long)g_group_telemetry.event_drops,
    (unsigned long)g_group_telemetry.button_presses,
    (unsigned long)g_group_telemetry.guardian_level,
    (unsigned long)g_group_telemetry.guardian_events,
    (unsigned long)g_group_telemetry.guardian_recoveries);
  console_write(g_r21_line);
}
/* Nucleo-L476RG B1 is PC13, active-low.  NOT valid for arbitrary custom PCB.
 * Poll from low-rate ControlTask; never start a separate scheduler/ISR here. */
#define GROUP_NUCLEO_B1_ENABLED 1
static void group_button_init(void)
{
#if GROUP_NUCLEO_B1_ENABLED
  GPIO_InitTypeDef pin = {0};
  __HAL_RCC_GPIOC_CLK_ENABLE();
  pin.Pin=GPIO_PIN_13; pin.Mode=GPIO_MODE_INPUT; pin.Pull=GPIO_PULLUP;
  HAL_GPIO_Init(GPIOC, &pin);
  /* Nucleo-L476RG LD2: PA5.  Custom hearing-aid PCB does NOT
   * have this indicator; this is a development-board-only output. */
  __HAL_RCC_GPIOA_CLK_ENABLE();
  pin.Pin=GPIO_PIN_5;pin.Mode=GPIO_MODE_OUTPUT_PP;
  pin.Pull=GPIO_NOPULL;pin.Speed=GPIO_SPEED_FREQ_LOW;
  HAL_GPIO_Init(GPIOA,&pin);
  HAL_GPIO_WritePin(GPIOA,GPIO_PIN_5,GPIO_PIN_RESET);
#endif
}
static void group_button_poll(uint32_t now)
{
#if GROUP_NUCLEO_B1_ENABLED
  static uint32_t changed_ms, down_ms;
  static uint8_t previous_raw=0U, stable=0U;
  /* Solid when a manual profile is active; 100-ms heartbeat in AUTO. */
  GPIO_PinState led=GPIO_PIN_RESET;
  if (g_group_telemetry.enabled) {
    if (!g_group_telemetry.automatic || (now%1000U)<100U) led=GPIO_PIN_SET;
  }
  HAL_GPIO_WritePin(GPIOA,GPIO_PIN_5,led);
  uint8_t raw=(HAL_GPIO_ReadPin(GPIOC,GPIO_PIN_13)==GPIO_PIN_RESET)?1U:0U;
  if (raw!=previous_raw) {previous_raw=raw; changed_ms=now;}
  if (raw!=stable && (uint32_t)(now-changed_ms)>=45U) {
    stable=raw;
    if (stable) down_ms=now;
    else {
      uint32_t pressed=now-down_ms;
      if (pressed>=1200U) {
        /* Long press toggles deterministic automatic loudness limiting. */
        group_event_send(GROUP_EVENT_ENABLE,1U);
        group_event_send(GROUP_EVENT_AUTO,
                         (uint8_t)!g_group_telemetry.automatic);
      } else if (pressed>=45U) {
        /* Short press cycles four real audio profiles. */
        group_event_send(GROUP_EVENT_ENABLE,1U);
        group_event_send(GROUP_EVENT_PROFILE,
                (uint8_t)((g_group_telemetry.profile+1U)&3U));
      }
      g_group_button_seen++;
      g_group_telemetry.button_presses=g_group_button_seen;
    }
  }
#else
  (void)now;
#endif
}
static void group_audio_update(void)
{
  /* Sample existing actual DMA/audio fault counters at a bounded rate.
   * This guardian bypasses only optional G1 DSP, never R34 audio transport. */
  uint32_t faults = g_demo_diag.speaker_underruns +
                    g_demo_diag.bt_dma_overruns +
                    g_demo_diag.tc_dma_overruns +
                    g_demo_diag.service_deadline_misses;
  uint32_t depth = g_demo_diag.bt_capture_depth;
  if (g_demo_diag.tc_capture_depth > depth)
    depth=g_demo_diag.tc_capture_depth;
  uint8_t was_bypassed=g_group_guard_bypass;
  g_group_guard_bypass=(uint8_t)(GroupGuardian_Update(&g_group_guardian,
      HAL_GetTick(),faults,depth)!=GROUP_GUARD_NORMAL);
  /* Restart optional gain at unity after overload, then ramp to its target
   * instead of abruptly reapplying yesterday's attenuation. */
  if (was_bypassed && !g_group_guard_bypass && g_group_features.enabled)
    GroupFeatures_Enable(&g_group_features,1U);
  GroupEvent ev;
  uint32_t count=0U;
  while (g_group_events && count<8U && xQueueReceive(g_group_events,&ev,0U)==pdPASS) {
    if (ev.type==GROUP_EVENT_ENABLE) GroupFeatures_Enable(&g_group_features,ev.value);
    if (ev.type==GROUP_EVENT_PROFILE) GroupFeatures_Select(&g_group_features,(GroupProfile)ev.value);
    if (ev.type==GROUP_EVENT_AUTO) GroupFeatures_Auto(&g_group_features,ev.value);
    ++g_group_events_seen;
    ++count;
  }
  /* Profile manager has a single AudioTask owner.  When enabled in NORMAL
   * routed mode, reuse the existing well-tested compressor/resonance EQ.
   * Music preserves dynamics; voice/wind favour speech processing. Direct
   * reference playback remains DSP-bypassed (only optional group gain/HPF). */
  if (g_group_features.enabled && !g_group_guard_bypass && !g_group_dsp_saved) {
    g_group_old_dsp_on=(uint8_t)g_demo_control.dsp_enabled;
    g_group_old_comp=(uint8_t)g_dynamics.compressor_enabled;
    g_group_old_eq=(uint8_t)g_dynamics.resonance_eq_enabled;
    g_group_dsp_saved=1U;
    g_group_last_applied_profile=0xFFU;
  }
  if (g_group_features.enabled && !g_group_guard_bypass &&
      g_group_features.profile!=g_group_last_applied_profile) {
    if (g_group_features.profile==GROUP_PROFILE_MUSIC) {
      AudioDynamics_SetCompression(&g_dynamics,false);
      AudioDynamics_SetResonanceEq(&g_dynamics,false);
    } else if (g_group_features.profile==GROUP_PROFILE_VOICE ||
               g_group_features.profile==GROUP_PROFILE_WIND) {
      AudioDynamics_SetCompression(&g_dynamics,true);
      AudioDynamics_SetResonanceEq(&g_dynamics,true);
    } else {
      AudioDynamics_SetCompression(&g_dynamics,g_group_old_comp!=0U);
      AudioDynamics_SetResonanceEq(&g_dynamics,g_group_old_eq!=0U);
    }
    g_group_last_applied_profile=g_group_features.profile;
  }
  if ((!g_group_features.enabled || g_group_guard_bypass) && g_group_dsp_saved) {
    AudioDynamics_SetCompression(&g_dynamics,g_group_old_comp!=0U);
    AudioDynamics_SetResonanceEq(&g_dynamics,g_group_old_eq!=0U);
    g_demo_control.dsp_enabled=g_group_old_dsp_on;
    g_group_dsp_saved=0U;
    g_group_last_applied_profile=0xFFU;
  }
  g_group_telemetry.enabled=g_group_features.enabled;
  g_group_telemetry.automatic=g_group_features.automatic;
  g_group_telemetry.profile=g_group_features.profile;
  g_group_telemetry.scene=g_group_features.scene;
  g_group_telemetry.gain_q15=g_group_features.gain_q15;
  g_group_telemetry.input_mean_abs=g_group_features.recent_mean_abs;
  g_group_telemetry.processed_blocks=g_group_features.processed_blocks;
  g_group_telemetry.scene_updates=g_group_features.scene_updates;
  g_group_telemetry.clipped=g_group_features.saturated_samples;
  g_group_telemetry.event_count=g_group_events_seen;
  g_group_telemetry.guardian_level=g_group_guardian.level;
  g_group_telemetry.guardian_events=g_group_guardian.events;
  g_group_telemetry.guardian_recoveries=g_group_guardian.recoveries;
}

static bool r25_process_short_command(const char *line)
{
  /* R25 control-plane protocol: very short framed commands reduce the chance
   * of USART2 byte loss under simultaneous SAI1/SAI2 DMA traffic.  A leading
   * '!' is also a resynchronisation marker in poll_console(), so even if a
   * previous CR/LF is lost the next command starts a clean frame. */
  if (line_eq(line, "!GP")) { group_print_status(); return true; }
  if (line_eq(line, "!GON")) {group_event_send(GROUP_EVENT_ENABLE,1U); console_write("GROUP|ACK|ON\r\n"); return true;}
  if (line_eq(line, "!GOFF")) {group_event_send(GROUP_EVENT_ENABLE,0U); console_write("GROUP|ACK|OFF\r\n"); return true;}
  if (line_eq(line, "!GA")) {group_event_send(GROUP_EVENT_ENABLE,1U);group_event_send(GROUP_EVENT_AUTO,1U);console_write("GROUP|ACK|AUTO_LEVEL\r\n");return true;}
  if (line_eq(line, "!GM")) {group_event_send(GROUP_EVENT_AUTO,0U);console_write("GROUP|ACK|MANUAL\r\n");return true;}
  if (line_eq(line, "!G0") || line_eq(line, "!G1") || line_eq(line, "!G2") || line_eq(line, "!G3")) {
    group_event_send(GROUP_EVENT_ENABLE,1U);
    group_event_send(GROUP_EVENT_PROFILE,(uint8_t)(line[2]-'0'));
    console_write("GROUP|ACK|PROFILE_QUEUED\r\n");return true;
  }
  if (line_eq(line, "!HI")) { console_write("R21|HELLO|PASS|fw=28|base=R21_AUDIO|ctrl=R28|build=R34_PRIORITY7_CONTINUOUS_SPIN|group=G1_ADAPTIVE_BASE\r\n"); return true; }
  if (line_eq(line, "!H"))  { r21_health(); return true; }
  if (line_eq(line, "!OS")) { r21_output_start(); return true; }
  if (line_eq(line, "!OC")) { r21_output_check(); return true; }
  if (line_eq(line, "!OX")) { r21_output_stop(); return true; }
  if (line_eq(line, "!BR")) { console_write("R21|BT_START|PASS|mode=R9_RAW|gain=25|ack=PRESTART\r\n"); r21_start_source(DEMO_DIRECT_BT, 0U); return true; }
  if (line_eq(line, "!BS")) { console_write("R21|BT_START|PASS|mode=AUTO_LEVEL_MONO|ack=PRESTART\r\n"); r21_start_source(DEMO_DIRECT_BT, 1U); return true; }
  if (line_eq(line, "!BC")) { r21_bt_check(); return true; }
  if (line_eq(line, "!TS")) { console_write("R21|TC_START|PASS|mode=SAI2_AUTO_LEVEL|ack=PRESTART\r\n"); r21_start_source(DEMO_DIRECT_TC, 1U); return true; }
  if (line_eq(line, "!TC")) { r21_tc_check(); return true; }
  if (line_eq(line, "!TP")) { r24_tc_clock_probe(); return true; }
  if (line_eq(line, "!SP")) { r21_scope(); return true; }
  if (line_eq(line, "!SH")) { r262_scope_hd(); return true; }
  if (line_eq(line, "!DP")) { r27_scope_dual(); return true; }
  if (line_eq(line, "!TN")) { g_r27_tc_clean_enabled=1U; TcCleanup_Reset(&g_r27_tc_filter); console_write("R27|TC_CLEAN|PASS|enabled=1\r\n"); return true; }
  if (line_eq(line, "!TF")) { g_r27_tc_clean_enabled=0U; TcCleanup_Reset(&g_r27_tc_filter); console_write("R27|TC_CLEAN|PASS|enabled=0\r\n"); return true; }
  if (line_eq(line, "!DG")) { r21_diag(); return true; }
  if (line_eq(line, "!FA")) { r28_real_route_arm(); return true; }
  if (line_eq(line, "!FK")) {
    if (g_demo_control.direct_source != DEMO_DIRECT_OFF ||
        g_r28_route_restart_pending || !g_dma_started || !g_bt_dma_started) {
      console_write("R28|CAL|REJECTED|reason=route_not_ready\r\n");
    } else {
      g_r28_cal_blocks=0U; g_r28_cal_max_rms=0U; g_r28_cal_max_pp=0U;
      g_r28_cal_state=1U;
      console_write("R28|CAL|ARMED|input=REAL_SAI1_PCM|windows=192\r\n");
    }
    return true;
  }
  if (line_eq(line, "!FV")) { r28_real_fsm_observe(); return true; }
  if (line_eq(line, "!RM")) { r28_rtos_snapshot(); return true; }
  if (line_eq(line, "!PX")) {
    const uint32_t clk_on =
        ((RCC->APB1ENR1 & RCC_APB1ENR1_TIM2EN) != 0U) ? 1U : 0U;
    const uint32_t cen = ((TIM2->CR1 & TIM_CR1_CEN) != 0U) ? 1U : 0U;
    const uint32_t prescaler = TIM2->PSC;
    const uint32_t t0 = Demo_RuntimeStatsTimerRead();
    vTaskDelay(pdMS_TO_TICKS(2U));
    const uint32_t t1 = Demo_RuntimeStatsTimerRead();
    (void)snprintf(g_r21_line,sizeof(g_r21_line),
      "R30|PROFILE_HW|DATA|clk=%lu|cen=%lu|psc=%lu|cnt0=%lu|cnt1=%lu|dt=%lu|init=%lu|rearm=%lu|freeze=%lu\r\n",
      (unsigned long)clk_on, (unsigned long)cen,
      (unsigned long)prescaler, (unsigned long)t0, (unsigned long)t1,
      (unsigned long)(t1-t0),
      (unsigned long)Demo_RuntimeStatsTimerInitCount(),
      (unsigned long)Demo_RuntimeStatsTimerReenableCount(),
      (unsigned long)((DBGMCU->APB1FZR1 & DBGMCU_APB1FZR1_DBG_TIM2_STOP) != 0U));
    console_write(g_r21_line);
    return true;
  }
  if (line_eq(line, "!PV")) {
    (void)snprintf(g_r21_line,sizeof(g_r21_line),
      "R29|PROOF|DATA|seq=%lu|valid=%lu|cpu10=%lu|bg10=%lu|aud10=%lu|ctl10=%lu|bt10=%lu|idle10=%lu|load=%lu|ct=%lu|at=%lu|bt=%lu|gt=%lu|work=%lu|rxbt=%lu|rxtc=%lu|tx=%lu|und=%lu|mode=%lu|src=%lu|sw=%lu|direct=%lu|reason=%lu|timer=%lu|kernel=%lu|dt=%lu|kdt=%lu|wall=%lu|runsum=%lu|tasks=%lu|method=%lu|tsw=%lu|tov=%lu|ticks=%lu|tdrop=%lu|b2=%lu|w2=%lu|b2cpu10=%lu|b2state=%lu|b2stack=%lu|dual=%lu|hi7=%lu|h7loops=%lu|h7work=%lu|h7cpu10=%lu|h7state=%lu|h7stack=%lu\r\n",
      (unsigned long)g_demo_debug.sample_number,(unsigned long)g_demo_debug.valid,
      (unsigned long)g_demo_debug.total_cpu_pct_x10,(unsigned long)g_demo_debug.background_cpu_pct_x10,
      (unsigned long)g_demo_debug.audio_cpu_pct_x10,(unsigned long)g_demo_debug.control_cpu_pct_x10,
      (unsigned long)g_demo_debug.bluetooth_cpu_pct_x10,(unsigned long)g_demo_debug.idle_cpu_pct_x10,
      (unsigned long)g_demo_debug.load_mode,
      (unsigned long)g_demo_debug.control_loops,(unsigned long)g_demo_debug.audio_loops,
      (unsigned long)g_demo_debug.bt_loops,(unsigned long)g_demo_debug.background_loops,
      (unsigned long)g_demo_debug.background_work_blocks,
      (unsigned long)g_demo_debug.bt_dma_callbacks,(unsigned long)g_demo_debug.tc_dma_callbacks,
      (unsigned long)g_demo_debug.speaker_dma_blocks,(unsigned long)g_demo_debug.speaker_underruns,
      (unsigned long)g_demo_debug.fsm_mode,(unsigned long)g_demo_debug.selected_source,
      (unsigned long)g_demo_debug.real_fsm_switches,(unsigned long)g_demo_debug.direct_mode,
      (unsigned long)g_demo_debug.profile_reason,
      (unsigned long)g_demo_debug.profile_timer_count,
      (unsigned long)g_demo_debug.profile_kernel_total,
      (unsigned long)g_demo_debug.profile_timer_delta,
      (unsigned long)g_demo_debug.profile_kernel_delta,
      (unsigned long)g_demo_debug.profile_wall_delta_ms,
      (unsigned long)g_demo_debug.profile_accounted_delta,
      (unsigned long)g_demo_debug.task_count,
      (unsigned long)g_demo_debug.profile_source,
      (unsigned long)g_demo_debug.profile_trace_switches,
      (unsigned long)g_demo_debug.profile_trace_overflow,
      (unsigned long)g_demo_debug.profile_tick_samples,
      (unsigned long)g_demo_debug.profile_tick_dropped,
      (unsigned long)g_demo_debug.load_b_loops,
      (unsigned long)g_demo_debug.load_b_work_blocks,
      (unsigned long)g_demo_debug.load_b_cpu_pct_x10,
      (unsigned long)g_demo_debug.load_b_state,
      (unsigned long)g_demo_debug.load_b_free_stack_words,
      (unsigned long)g_demo_debug.dual_load_enabled,
      (unsigned long)g_demo_debug.high7_enabled,
      (unsigned long)g_demo_debug.high7_loops,
      (unsigned long)g_demo_debug.high7_work_blocks,
      (unsigned long)g_demo_debug.high7_cpu_pct_x10,
      (unsigned long)g_demo_debug.high7_state,
      (unsigned long)g_demo_debug.high7_free_stack_words);
    console_write(g_r21_line); return true;
  }
  if (line_eq(line, "!RA")) {
    g_r32_hi_active=0U;
    g_r31_dual_load = 0U;
    g_demo_control.background_load_enabled = 1U;
    console_write("R28|LOAD|ARMED|enabled=1|physical_tasks=1\r\n"); return true;
  }
  if (line_eq(line, "!RX")) {
    g_r32_hi_active=0U;
    g_r31_dual_load = 0U;
    g_demo_control.background_load_enabled = 0U;
    console_write("R28|LOAD|ARMED|enabled=0|physical_tasks=1\r\n"); return true;
  }
  if (line_eq(line, "!RS")) {
    g_r32_hi_active=0U;
    g_r31_dual_load = 0U; /* keep the original single worker for comparison */
    /* Bounded low-priority CPU saturation for a live scheduler-debug demo.
     * The ControlTask still services !RX while high-priority audio runs.
     * Use !RM at two instants to calculate real CPU usage from TIM2.
     */
    g_stress_stop_at_ms = HAL_GetTick() + DEMO_STRESS_DURATION_MS;
    g_demo_control.background_load_enabled = 2U;
    console_write("R28|LOAD|ARMED|enabled=2|mode=SATURATION|duration_ms=60000|auto_off=1\r\n");
    return true;
  }
  if (line_eq(line, "!RD")) {
    g_r32_hi_active=0U;
    /* Two equal-priority, CPU-bound workers share spare Cortex-M4 time.
     * No dangerous priority change to AudioTask or runtime profiler. */
    if (g_cpu_load_b_task == NULL || g_background_task == NULL) {
      console_write("R28|LOAD|FAIL|reason=dual_tasks_not_created\r\n");
      return true;
    }
    g_stress_stop_at_ms = HAL_GetTick() + DEMO_STRESS_DURATION_MS;
    g_r31_dual_load = 1U;
    g_demo_control.background_load_enabled = 2U;
    console_write("R28|LOAD|ARMED|enabled=2|mode=DUAL_EQUAL_PRIORITY|workers=2|priority=2|duration_ms=60000|auto_off=1\r\n");
    return true;
  }
  if (line_eq(line, "!RH")) {
    /* R34: a GENUINE continuously-ready priority-7 CPU load task.
     * There is NO voluntary delay or yield while the 20s window is active.
     * As a consequence ControlTask(6), BtTask(5), and UART commands (including
     * !RX/!PV) can be starved until it self-disarms. This is INTENTIONAL
     * for the teaching demonstration and must not be used in production.
     * TIM6 HAL_GetTick() continues in interrupt context to enforce the
     * deadline even while the priority-7 task monopolises spare CPU time.
     * The hardware ST-LINK debugger can suspend the MCU at any time.
     */
    if (g_r32_hi_task == NULL) {
      console_write("R28|LOAD|FAIL|reason=high7_task_not_created\r\n");
      return true;
    }
    g_r31_dual_load = 0U;
    g_demo_control.background_load_enabled = 0U;
    g_r32_hi_deadline_ms = HAL_GetTick() + R34_SPIN_DEADLINE_MS;
    /* Transmit the ACK BEFORE enabling the spin task so the PC receives it
     * even when ControlTask then stops being scheduled for 20 seconds. */
    console_write("R28|LOAD|ARMED|enabled=3|mode=PRIORITY7_CONTINUOUS_SPIN|priority=7|delay_ticks=0|duration_ms=20000|auto_off=1|control_starved=1\r\n");
    g_r32_hi_active = 1U;
    return true;
  }
  if (line_eq(line, "!FS") || line_eq(line, "!FC")) {
    console_write("R28|FSM|UNSUPPORTED|reason=synthetic_test_removed|use=!FA,!FV\r\n");
    return true;
  }
  if (line_eq(line, "!RT")) {
    uint32_t c0=g_demo_diag.control_task_loops, a0=g_demo_diag.audio_task_loops;
    uint32_t b0=g_demo_diag.bt_task_loops, g0=g_demo_diag.background_task_loops;
    uint32_t w0=g_demo_diag.background_work_iterations, m0=g_demo_diag.service_deadline_misses;
    /* The control task blocks while the other FreeRTOS tasks remain runnable.
     * This is real cooperative/pre-emptive scheduler concurrency on ONE CPU,
     * not literal simultaneous execution across processor cores. */
    g_r31_dual_load=0U; g_demo_control.background_load_enabled=1U; vTaskDelay(pdMS_TO_TICKS(900U)); g_demo_control.background_load_enabled=0U;
    uint32_t dc=g_demo_diag.control_task_loops-c0, da=g_demo_diag.audio_task_loops-a0;
    uint32_t db=g_demo_diag.bt_task_loops-b0, dg=g_demo_diag.background_task_loops-g0;
    uint32_t dw=g_demo_diag.background_work_iterations-w0, dm=g_demo_diag.service_deadline_misses-m0;
    uint32_t ok=(da>20U && db>5U && dg>2U && dw>0U && dm==0U);
    (void)snprintf(g_r21_line,sizeof(g_r21_line),"R21|RTOS|%s|ctl=%lu|aud=%lu|bt=%lu|bg=%lu|work=%lu|miss=%lu|ms=900|cpri=6|apri=7|bpri=5|gpri=2\r\n",
      r21_name(ok),(unsigned long)dc,(unsigned long)da,(unsigned long)db,(unsigned long)dg,(unsigned long)dw,(unsigned long)dm);
    console_write(g_r21_line);
    return true;
  }
  return false;
}

/* Final demonstration: NEVER inject PCM or fake input flags. The actual
 * ConnectivityService receives BT/TC observations from SAI DMA and the
 * hardware-derived signal detectors. Diagnostic responses are DATA, not PASS.
 * Enabling the test only selects the normal production routing mode. */
static void r28_real_route_arm(void)
{
  g_demo_control.test_tone_step = 0U;
  g_demo_control.bt_link_mode = DEMO_BTLINK_AUTO;
  g_demo_control.requested_source_mode = DEMO_MODE_AUTO;
  g_demo_control.direct_source = DEMO_DIRECT_OFF;
  g_demo_control.audio_capture_enabled = 1U;
  g_demo_control.output_enabled = 1U;
  g_demo_control.plot_enabled = 0U;
  g_demo_control.dsp_enabled = 0U; /* preserve proven BT; TC cleanup stays active */
  g_demo_control.output_gain_q15 = 16384U;
  /* Do not reset ConnectivityService from ControlTask while AudioTask is
   * accessing it. This flag requests a clean REAL FSM session owned by the
   * audio task. Both actual ADCs must qualify their real samples anew. */
  g_demo_control.bt_present_rms_threshold = 350U;
  g_demo_control.bt_present_pp_threshold = 900U;
  g_r28_cal_state = 0U;
  g_r28_route_restart_pending = 1U;
  console_write("R28|AUTO|ARMED|input=PHYSICAL_DMA|link=AUTO|direct=0|synthetic=0\r\n");
}

static void r28_real_fsm_observe(void)
{
  /* Do not set/override source validity. Returns snapshots from actual DMA,
   * detector and controller. Source: 0 none, 1 TC, 2 BT. */
  (void)snprintf(g_r21_line,sizeof(g_r21_line),
    "R28|LIVE|DATA|ms=%lu|mode=%lu|src=%lu|btvalid=%lu|tcvalid=%lu|btrms=%lu|tcrms=%lu|btpp=%lu|tcpp=%lu|tcclipped=%lu|btthr=%lu|ppthr=%lu|btcb=%lu|tccb=%lu|bblk=%lu|tblk=%lu|spk=%lu|sw=%lu|fault=%lu|under=%lu|berr=%lu|terr=%lu|direct=%lu|epoch=%lu|ready=%lu|cal=%lu|calrms=%lu|calpp=%lu|synth=0\r\n",
    (unsigned long)HAL_GetTick(), (unsigned long)g_demo_diag.mode,
    (unsigned long)g_demo_diag.active_source,
    (unsigned long)g_demo_diag.bt_audio_present, (unsigned long)g_demo_diag.tc_valid,
    (unsigned long)g_demo_diag.bt_rms, (unsigned long)g_demo_diag.tc_rms,
    (unsigned long)g_demo_diag.bt_peak_to_peak,
    (unsigned long)g_demo_diag.tc_peak_to_peak,
    (unsigned long)g_service.last_telecoil_metrics.clipped_samples,
    (unsigned long)g_demo_control.bt_present_rms_threshold,
    (unsigned long)g_demo_control.bt_present_pp_threshold,
    (unsigned long)(g_demo_diag.bt_half_callbacks+g_demo_diag.bt_full_callbacks),
    (unsigned long)(g_demo_diag.tc_half_callbacks+g_demo_diag.tc_full_callbacks),
    (unsigned long)g_demo_diag.bt_blocks_processed,
    (unsigned long)g_demo_diag.tc_blocks_processed,
    (unsigned long)g_demo_diag.speaker_blocks_output,
    (unsigned long)g_demo_diag.source_switches,
    (unsigned long)g_demo_diag.fault,
    (unsigned long)g_demo_diag.speaker_underruns,
    (unsigned long)g_demo_diag.sai1_errors, (unsigned long)g_demo_diag.sai2_errors,
    (unsigned long)g_demo_control.direct_source,
    (unsigned long)g_r28_route_epoch,
    (unsigned long)((g_r28_route_epoch != 0U) && !g_r28_route_restart_pending &&
                    g_dma_started && g_bt_dma_started && g_tc_dma_started &&
                    (g_demo_control.direct_source == DEMO_DIRECT_OFF)),
    (unsigned long)g_r28_cal_state,
    (unsigned long)g_r28_cal_max_rms,
    (unsigned long)g_r28_cal_max_pp);
  console_write(g_r21_line);
}

static uint32_t r28_task_state(TaskHandle_t handle)
{
  return handle == NULL ? 255U : (uint32_t)eTaskGetState(handle);
}

/* Independent, debugger-readable RTOS evidence, refreshed at 2 Hz. This
 * sampler reads FreeRTOS's actual task status and TIM2 run-time counters.
 * It never sets state-machine inputs, forces task states, or fabricates load.
 * A debugger halt freezes all tasks AND this snapshot; resume to get new data.
 */
static void r29_refresh_debug_snapshot(uint32_t ms)
{
  /* R30.2: do not gate valid data on an arbitrary 2s maximum interval.
   * A busy ControlTask or a long serial transfer can postpone an observation.
   * Measure the actual elapsed TIM2 ticks, and expose a failure reason when
   * CPU percentage measurements genuinely cannot be computed. */
  static uint32_t last_ms;
  static uint32_t prev_total, prev_timer, prev_tick_total;
  static uint32_t prev_ctl, prev_aud, prev_bt, prev_bg, prev_b2, prev_hi7, prev_idle;
  static uint8_t primed;
  static uint32_t previous_source;
  TaskStatus_t rec[10];
  uint32_t kernel_total=0U, ctl=0U, aud=0U, bt=0U, bg=0U, b2=0U, hi7=0U, idle=0U;
  const uint32_t timer_now=Demo_RuntimeStatsTimerRead();
  const uint32_t tick_now=Demo_TickProfilerTotalSamples();
  const uint32_t wall_ms=(last_ms==0U)?0U:(ms-last_ms);
  UBaseType_t n;
  if (last_ms!=0U && wall_ms<500U) return;
  last_ms=ms;
  n=uxTaskGetSystemState(rec,(UBaseType_t)10U,&kernel_total);
  g_demo_debug.signature=0x52544F53UL;
  g_demo_debug.sample_number++;
  g_demo_debug.board_time_ms=ms;
  g_demo_debug.scheduler_state=(uint32_t)xTaskGetSchedulerState();
  g_demo_debug.task_count=(uint32_t)n;
  g_demo_debug.profile_timer_count=timer_now;
  g_demo_debug.profile_kernel_total=kernel_total;
  g_demo_debug.profile_tick_samples=tick_now;
  g_demo_debug.profile_tick_dropped=Demo_TickProfilerDroppedSamples();
  g_demo_debug.profile_wall_delta_ms=wall_ms;
  g_demo_debug.profile_reason=1U; /* warmup until proved otherwise */
  g_demo_debug.profile_timer_delta=0U;
  g_demo_debug.profile_kernel_delta=0U;
  g_demo_debug.profile_accounted_delta=0U;
  g_demo_debug.valid=0U;
  g_demo_debug.load_mode=g_demo_control.background_load_enabled;
  g_demo_debug.control_loops=g_demo_diag.control_task_loops;
  g_demo_debug.audio_loops=g_demo_diag.audio_task_loops;
  g_demo_debug.bt_loops=g_demo_diag.bt_task_loops;
  g_demo_debug.background_loops=g_demo_diag.background_task_loops;
  g_demo_debug.background_work_blocks=g_demo_diag.background_work_iterations;
  g_demo_debug.load_b_loops=g_r31_cpu_b_loops;
  g_demo_debug.load_b_work_blocks=g_r31_cpu_b_work_blocks;
  g_demo_debug.dual_load_enabled=g_r31_dual_load;
  g_demo_debug.high7_enabled=g_r32_hi_active;
  g_demo_debug.high7_loops=g_r32_hi_loops;
  g_demo_debug.high7_work_blocks=g_r32_hi_work_blocks;
  g_demo_debug.bt_dma_callbacks=g_demo_diag.bt_half_callbacks+g_demo_diag.bt_full_callbacks;
  g_demo_debug.tc_dma_callbacks=g_demo_diag.tc_half_callbacks+g_demo_diag.tc_full_callbacks;
  g_demo_debug.speaker_dma_blocks=g_demo_diag.speaker_blocks_output;
  g_demo_debug.speaker_underruns=g_demo_diag.speaker_underruns;
  g_demo_debug.fsm_mode=g_demo_diag.mode;
  g_demo_debug.selected_source=g_demo_diag.active_source;
  g_demo_debug.bt_detected=g_demo_diag.bt_audio_present;
  g_demo_debug.tc_detected=g_demo_diag.tc_valid;
  g_demo_debug.real_fsm_switches=g_demo_diag.source_switches;
  g_demo_debug.real_fsm_fault=g_demo_diag.fault;
  g_demo_debug.direct_mode=g_demo_control.direct_source;

  if (n==0U) {
    g_demo_debug.profile_reason=2U; /* uxTaskGetSystemState failed */
    primed=0U;
    return;
  }
  TaskHandle_t control=xTaskGetCurrentTaskHandle();
  for (UBaseType_t i=0U;i<n;++i) {
    if (rec[i].xHandle==control) ctl=(uint32_t)rec[i].ulRunTimeCounter;
    else if (rec[i].xHandle==g_audio_task) aud=(uint32_t)rec[i].ulRunTimeCounter;
    else if (rec[i].xHandle==g_bt_task) bt=(uint32_t)rec[i].ulRunTimeCounter;
    else if (rec[i].xHandle==g_background_task) bg=(uint32_t)rec[i].ulRunTimeCounter;
    else if (rec[i].xHandle==g_cpu_load_b_task) b2=(uint32_t)rec[i].ulRunTimeCounter;
    else if (rec[i].xHandle==g_r32_hi_task) hi7=(uint32_t)rec[i].ulRunTimeCounter;
    else if (strcmp(rec[i].pcTaskName,"IDLE")==0) idle=(uint32_t)rec[i].ulRunTimeCounter;
  }
  /* STM32CubeL4 FreeRTOS may return all-zero kernel runtime fields even
   * though its scheduler and TIM2 are running.  Fall back to an independent,
   * real context-switch trace hooked in FreeRTOSConfig.h.  This is actual
   * scheduled CPU time, not loop-count-estimated utilisation. */
  uint32_t source=1U;
  const uint64_t native_sum=(uint64_t)ctl+aud+bt+bg+b2+hi7+idle;
  if (native_sum==0U) {
    uint32_t tc=Demo_TraceGetTaskTime((void *)control);
    uint32_t ta=Demo_TraceGetTaskTime((void *)g_audio_task);
    uint32_t tb=Demo_TraceGetTaskTime((void *)g_bt_task);
    uint32_t tg=Demo_TraceGetTaskTime((void *)g_background_task);
    uint32_t t2=Demo_TraceGetTaskTime((void *)g_cpu_load_b_task);
    uint32_t th=Demo_TraceGetTaskTime((void *)g_r32_hi_task);
    uint32_t ti=0U;
    for (UBaseType_t k=0U;k<n;++k) {
      if (strcmp(rec[k].pcTaskName,"IDLE")==0)
        ti=Demo_TraceGetTaskTime((void *)rec[k].xHandle);
    }
    ctl=tc; aud=ta; bt=tb; bg=tg; b2=t2; hi7=th; idle=ti;
    source=2U;
    /* If the generated FreeRTOS kernel contains neither the runtime-stats
     * nor context-switch hooks, use real TIM6 ISR samples instead of zero
     * values.  This is sampled CPU residency, with explicit method=3. */
    if (((uint64_t)ctl+aud+bt+bg+b2+hi7+idle)==0U) {
      ctl=Demo_TickProfilerTaskSamples((void *)control);
      aud=Demo_TickProfilerTaskSamples((void *)g_audio_task);
      bt=Demo_TickProfilerTaskSamples((void *)g_bt_task);
      bg=Demo_TickProfilerTaskSamples((void *)g_background_task);
      b2=Demo_TickProfilerTaskSamples((void *)g_cpu_load_b_task);
      hi7=Demo_TickProfilerTaskSamples((void *)g_r32_hi_task);
      idle=0U;
      for (UBaseType_t k=0U;k<n;++k) {
        if (strcmp(rec[k].pcTaskName,"IDLE")==0)
          idle=Demo_TickProfilerTaskSamples((void *)rec[k].xHandle);
      }
      source=3U;
    }
  }
  g_demo_debug.profile_source=source;
  g_demo_debug.profile_trace_switches=Demo_TraceGetSwitches();
  g_demo_debug.profile_trace_overflow=Demo_TraceGetOverflow();
  if (source != previous_source) { primed=0U; previous_source=source; }
  g_demo_debug.control_state=r28_task_state(control);
  g_demo_debug.audio_state=r28_task_state(g_audio_task);
  g_demo_debug.bt_state=r28_task_state(g_bt_task);
  g_demo_debug.background_state=r28_task_state(g_background_task);
  g_demo_debug.load_b_state=r28_task_state(g_cpu_load_b_task);
  g_demo_debug.high7_state=r28_task_state(g_r32_hi_task);
  g_demo_debug.control_free_stack_words=g_demo_diag.control_stack_high_water;
  g_demo_debug.audio_free_stack_words=g_demo_diag.audio_stack_high_water;
  g_demo_debug.bt_free_stack_words=g_demo_diag.bt_stack_high_water;
  g_demo_debug.background_free_stack_words=g_demo_diag.background_stack_high_water;
  g_demo_debug.load_b_free_stack_words=(g_cpu_load_b_task != NULL) ? (uint32_t)uxTaskGetStackHighWaterMark(g_cpu_load_b_task) : 0U;
  g_demo_debug.high7_free_stack_words=(g_r32_hi_task != NULL) ? (uint32_t)uxTaskGetStackHighWaterMark(g_r32_hi_task) : 0U;

  if (primed) {
    const uint32_t dt=timer_now-prev_timer; /* TIM2 wraps at ~71.6 min */
    const uint32_t kd=kernel_total-prev_total;
    const uint32_t dc=ctl-prev_ctl, da=aud-prev_aud, db=bt-prev_bt;
    const uint32_t dg=bg-prev_bg, d2=b2-prev_b2, dh=hi7-prev_hi7, di=idle-prev_idle;
    const uint64_t run_sum=(uint64_t)dc+da+db+dg+d2+dh+di;
    const uint32_t sample_dt=tick_now-prev_tick_total;
    /* Tick sampling uses its own interrupt-sample denominator.  Cycle-exact
     * methods 1 and 2 instead use the TIM2 microsecond interval. */
    const uint32_t fraction_denominator=(source==3U)?sample_dt:dt;
    g_demo_debug.profile_timer_delta=dt;
    g_demo_debug.profile_kernel_delta=kd;
    g_demo_debug.profile_accounted_delta=(run_sum>UINT32_MAX)?UINT32_MAX:(uint32_t)run_sum;
    g_demo_debug.measurement_interval_us=dt;
    if (wall_ms>60000U) {
      g_demo_debug.profile_reason=6U; /* runtime timer could have wrapped */
    } else if (dt==0U) {
      g_demo_debug.profile_reason=3U; /* TIM2 stalled or not initialized */
      /* Only re-enable disabled RCC/CEN bits; never reset a running counter. */
      if (Demo_RuntimeStatsTimerEnsureRunning() != 0U) primed=0U;
    } else if (dt<10000U) {
      g_demo_debug.profile_reason=5U; /* too little data for robust % */
    } else if (run_sum==0U || fraction_denominator==0U) {
      g_demo_debug.profile_reason=4U; /* task runtime accounting stalled */
    } else {
      /* These are real accumulated scheduler task runtimes: either the native
       * FreeRTOS counters or TIM2-backed context switch hooks (method=2).
       * Method 3 uses the actual number of 1kHz task samples.  Other
       * methods use the TIM2 microseconds. */
      #define R302_PCT10(v) ((uint32_t)((((uint64_t)(v)*1000ULL)/(uint64_t)fraction_denominator)>1000ULL?1000ULL:(((uint64_t)(v)*1000ULL)/(uint64_t)fraction_denominator)))
      g_demo_debug.control_cpu_pct_x10=R302_PCT10(dc);
      g_demo_debug.audio_cpu_pct_x10=R302_PCT10(da);
      g_demo_debug.bluetooth_cpu_pct_x10=R302_PCT10(db);
      g_demo_debug.background_cpu_pct_x10=R302_PCT10(dg);
      g_demo_debug.load_b_cpu_pct_x10=R302_PCT10(d2);
      g_demo_debug.high7_cpu_pct_x10=R302_PCT10(dh);
      g_demo_debug.idle_cpu_pct_x10=R302_PCT10(di);
      g_demo_debug.total_cpu_pct_x10=1000U-g_demo_debug.idle_cpu_pct_x10;
      #undef R302_PCT10
      g_demo_debug.valid=1U;
      g_demo_debug.profile_reason=0U;
    }
  }
  prev_timer=timer_now;
  prev_total=kernel_total;
  prev_tick_total=tick_now;
  prev_ctl=ctl;prev_aud=aud;prev_bt=bt;prev_bg=bg;prev_b2=b2;prev_hi7=hi7;prev_idle=idle;
  primed=1U;
}

static void r28_rtos_snapshot(void)
{
  TaskStatus_t state[10];
  UBaseType_t count;
  uint32_t total_time = 0U;
  uint32_t cuse=0U, ause=0U, buse=0U, guse=0U, b2use=0U, h7use=0U, iuse=0U;
  /* FreeRTOS kernel writes run-time accounting into the task status records.
   * TIM2 at 1 MHz measures actual scheduled task execution in microseconds.
   * Interrupt execution may be attributed to the interrupted task. */
  count = uxTaskGetSystemState(state,10U,&total_time);
  for (UBaseType_t i=0U; i<count; ++i) {
    if (state[i].xHandle == xTaskGetCurrentTaskHandle()) cuse = (uint32_t)state[i].ulRunTimeCounter;
    else if (state[i].xHandle == g_audio_task) ause = (uint32_t)state[i].ulRunTimeCounter;
    else if (state[i].xHandle == g_bt_task) buse = (uint32_t)state[i].ulRunTimeCounter;
    else if (state[i].xHandle == g_background_task) guse = (uint32_t)state[i].ulRunTimeCounter;
    else if (state[i].xHandle == g_cpu_load_b_task) b2use = (uint32_t)state[i].ulRunTimeCounter;
    else if (state[i].xHandle == g_r32_hi_task) h7use = (uint32_t)state[i].ulRunTimeCounter;
    else if (strcmp(state[i].pcTaskName,"IDLE") == 0) iuse=(uint32_t)state[i].ulRunTimeCounter;
  }
  if (cuse==0U && ause==0U && buse==0U && guse==0U && b2use==0U && h7use==0U && iuse==0U) {
    cuse=Demo_TraceGetTaskTime((void *)xTaskGetCurrentTaskHandle());
    ause=Demo_TraceGetTaskTime((void *)g_audio_task);
    buse=Demo_TraceGetTaskTime((void *)g_bt_task);
    guse=Demo_TraceGetTaskTime((void *)g_background_task);
    b2use=Demo_TraceGetTaskTime((void *)g_cpu_load_b_task);
    h7use=Demo_TraceGetTaskTime((void *)g_r32_hi_task);
    for (UBaseType_t k=0;k<count;++k)
      if (strcmp(state[k].pcTaskName,"IDLE")==0)
        iuse=Demo_TraceGetTaskTime((void *)state[k].xHandle);
    total_time=Demo_RuntimeStatsTimerRead();
  }
  (void)snprintf(g_r21_line,sizeof(g_r21_line),
    "R28|TASKS|DATA|ms=%lu|ctl=%lu|aud=%lu|bt=%lu|bg=%lu|work=%lu|cstate=%lu|astate=%lu|bstate=%lu|gstate=%lu|cstack=%lu|astack=%lu|bstack=%lu|gstack=%lu|btcb=%lu|tccb=%lu|spk=%lu|under=%lu|qov=%lu|bov=%lu|tov=%lu|miss=%lu|src=%lu|load=%lu|crun=%lu|arun=%lu|brun=%lu|grun=%lu|irun=%lu|tcnt=%lu|hz=1000000|cpri=%lu|apri=%lu|bpri=%lu|gpri=%lu|sched=%lu|b2=%lu|w2=%lu|b2state=%lu|b2stack=%lu|b2run=%lu|b2pri=%lu|dual=%lu|h7=%lu|h7work=%lu|h7run=%lu|h7state=%lu|h7pri=%lu|h7stack=%lu|hi7=%lu\r\n",
    (unsigned long)HAL_GetTick(),
    (unsigned long)g_demo_diag.control_task_loops,
    (unsigned long)g_demo_diag.audio_task_loops,
    (unsigned long)g_demo_diag.bt_task_loops,
    (unsigned long)g_demo_diag.background_task_loops,
    (unsigned long)g_demo_diag.background_work_iterations,
    (unsigned long)r28_task_state(xTaskGetCurrentTaskHandle()),
    (unsigned long)r28_task_state(g_audio_task),
    (unsigned long)r28_task_state(g_bt_task),
    (unsigned long)r28_task_state(g_background_task),
    (unsigned long)g_demo_diag.control_stack_high_water,
    (unsigned long)g_demo_diag.audio_stack_high_water,
    (unsigned long)g_demo_diag.bt_stack_high_water,
    (unsigned long)g_demo_diag.background_stack_high_water,
    (unsigned long)(g_demo_diag.bt_half_callbacks+g_demo_diag.bt_full_callbacks),
    (unsigned long)(g_demo_diag.tc_half_callbacks+g_demo_diag.tc_full_callbacks),
    (unsigned long)g_demo_diag.speaker_blocks_output,
    (unsigned long)g_demo_diag.speaker_underruns,
    (unsigned long)g_demo_diag.speaker_queue_overruns,
    (unsigned long)g_demo_diag.bt_dma_overruns,
    (unsigned long)g_demo_diag.tc_dma_overruns,
    (unsigned long)g_demo_diag.service_deadline_misses,
    (unsigned long)g_demo_diag.active_source,
    (unsigned long)g_demo_control.background_load_enabled,
    (unsigned long)cuse,(unsigned long)ause,(unsigned long)buse,(unsigned long)guse,
    (unsigned long)iuse,(unsigned long)total_time,
    (unsigned long)uxTaskPriorityGet(xTaskGetCurrentTaskHandle()),
    (unsigned long)uxTaskPriorityGet(g_audio_task),
    (unsigned long)uxTaskPriorityGet(g_bt_task),
    (unsigned long)uxTaskPriorityGet(g_background_task),
    (unsigned long)xTaskGetSchedulerState(),
    (unsigned long)g_r31_cpu_b_loops,
    (unsigned long)g_r31_cpu_b_work_blocks,
    (unsigned long)r28_task_state(g_cpu_load_b_task),
    (unsigned long)(g_cpu_load_b_task ? uxTaskGetStackHighWaterMark(g_cpu_load_b_task) : 0U),
    (unsigned long)b2use,
    (unsigned long)(g_cpu_load_b_task ? uxTaskPriorityGet(g_cpu_load_b_task) : 0U),
    (unsigned long)g_r31_dual_load,
    (unsigned long)g_r32_hi_loops,
    (unsigned long)g_r32_hi_work_blocks,
    (unsigned long)h7use,
    (unsigned long)r28_task_state(g_r32_hi_task),
    (unsigned long)(g_r32_hi_task ? uxTaskPriorityGet(g_r32_hi_task) : 0U),
    (unsigned long)(g_r32_hi_task ? uxTaskGetStackHighWaterMark(g_r32_hi_task) : 0U),
    (unsigned long)g_r32_hi_active);
  console_write(g_r21_line);
}

static bool r21_process_command(const char *line)
{
  if (r25_process_short_command(line)) return true;
  if (line_eq(line, "R21 HELLO")) { console_write("R21|HELLO|PASS|fw=28|base=R21_AUDIO|ctrl=R28|build=R34_PRIORITY7_CONTINUOUS_SPIN|group=G1_ADAPTIVE_BASE\r\n"); return true; }
  if (line_eq(line, "R21 HEALTH")) { r21_health(); return true; }
  if (line_eq(line, "R21 OUT START")) { r21_output_start(); return true; }
  if (line_eq(line, "R21 OUT CHECK")) { r21_output_check(); return true; }
  if (line_eq(line, "R21 OUT STOP")) { r21_output_stop(); return true; }
  if (line_eq(line, "R21 BT RAW")) { r21_start_source(DEMO_DIRECT_BT, 0U); console_write("R21|BT_START|PASS|mode=R9_RAW|gain=25\r\n"); return true; }
  if (line_eq(line, "R21 BT START")) { r21_start_source(DEMO_DIRECT_BT, 1U); console_write("R21|BT_START|PASS|mode=AUTO_LEVEL_MONO\r\n"); return true; }
  if (line_eq(line, "R21 BT CHECK")) { r21_bt_check(); return true; }
  if (line_eq(line, "R21 TC START")) { r21_start_source(DEMO_DIRECT_TC, 1U); console_write("R21|TC_START|PASS|mode=SAI2_AUTO_LEVEL\r\n"); return true; }
  if (line_eq(line, "R21 TC CHECK")) { r21_tc_check(); return true; }
  if (line_eq(line, "R21 SCOPE")) { r21_scope(); return true; }
  if (line_eq(line, "R21 FSM START") || line_eq(line, "R21 FSM CHECK")) {
    console_write("R28|FSM|UNSUPPORTED|reason=synthetic_test_removed|use=!FA,!FV\r\n");
    return true;
  }
  if (line_eq(line, "R21 RTOS")) {
    uint32_t c0=g_demo_diag.control_task_loops, a0=g_demo_diag.audio_task_loops;
    uint32_t b0=g_demo_diag.bt_task_loops, g0=g_demo_diag.background_task_loops;
    uint32_t w0=g_demo_diag.background_work_iterations, m0=g_demo_diag.service_deadline_misses;
    /* The control task blocks while the other FreeRTOS tasks remain runnable.
     * This is real cooperative/pre-emptive scheduler concurrency on ONE CPU,
     * not literal simultaneous execution across processor cores. */
    g_r31_dual_load=0U; g_demo_control.background_load_enabled=1U; vTaskDelay(pdMS_TO_TICKS(900U)); g_demo_control.background_load_enabled=0U;
    uint32_t dc=g_demo_diag.control_task_loops-c0, da=g_demo_diag.audio_task_loops-a0;
    uint32_t db=g_demo_diag.bt_task_loops-b0, dg=g_demo_diag.background_task_loops-g0;
    uint32_t dw=g_demo_diag.background_work_iterations-w0, dm=g_demo_diag.service_deadline_misses-m0;
    uint32_t ok=(da>20U && db>5U && dg>2U && dw>0U && dm==0U);
    (void)snprintf(g_r21_line,sizeof(g_r21_line),"R21|RTOS|%s|ctl=%lu|aud=%lu|bt=%lu|bg=%lu|work=%lu|miss=%lu|ms=900|cpri=6|apri=7|bpri=5|gpri=2\r\n",
      r21_name(ok),(unsigned long)dc,(unsigned long)da,(unsigned long)db,(unsigned long)dg,(unsigned long)dw,(unsigned long)dm);
    console_write(g_r21_line); return true;
  }
  if (line_eq(line, "R21 DIAG")) { r21_diag(); return true; }
  return false;
}

static void process_console_line(char *line)
{
  if (r21_process_command(line)) { g_demo_diag.console_commands_ok++; return; }
  g_demo_diag.console_lines++;
  if (line_eq(line, "PING")) { console_write("PONG - RTOS console RX/TX working\r\n"); g_demo_diag.console_commands_ok++; return; }
  if (line_eq(line, "HELP")) { print_help(); g_demo_diag.console_commands_ok++; return; }
  if (line_eq(line, "STATUS")) { print_status(); g_demo_diag.console_commands_ok++; return; }
  if (line_eq(line, "CLOCK")) { print_clock(); g_demo_diag.console_commands_ok++; return; }
  if (line_eq(line, "STREAM ON")) { g_demo_control.stream_enabled = 1U; console_write("OK stream on\r\n"); return; }
  if (line_eq(line, "STREAM OFF")) { g_demo_control.stream_enabled = 0U; console_write("OK stream off\r\n"); return; }
  if (line_eq(line, "LOAD ON")) { g_demo_control.background_load_enabled = 1U; console_write("OK dummy load on\r\n"); return; }
  if (line_eq(line, "LOAD OFF")) { g_r31_dual_load = 0U; g_demo_control.background_load_enabled = 0U; console_write("OK dummy load off\r\n"); return; }
  if (line_eq(line, "AUDIO START")) { g_demo_control.audio_capture_enabled = 1U; console_write("OK hardware audio start requested\r\n"); return; }
  if (line_eq(line, "AUDIO STOP")) { g_demo_control.audio_capture_enabled = 0U; console_write("OK hardware audio stop requested\r\n"); return; }
  if (line_eq(line, "AUDIO STATUS")) { print_test_result("AUDIO", test_audio()); print_status(); return; }
  if (line_eq(line, "OUTPUT ON")) { g_demo_control.output_enabled = 1U; console_write("OK bone-conductor output on\r\n"); return; }
  if (line_eq(line, "OUTPUT OFF")) { g_demo_control.output_enabled = 0U; output_queue_reset(); console_write("OK bone-conductor output muted\r\n"); return; }
  if (line_eq(line, "OUTPUT LEVEL SAFE")) { g_demo_control.output_gain_q15 = 4096U; console_write("OK output level SAFE (12.5%)\r\n"); return; }
  if (line_eq(line, "OUTPUT LEVEL LOW")) { g_demo_control.output_gain_q15 = 8192U; console_write("OK output level LOW (25%)\r\n"); return; }
  if (line_eq(line, "OUTPUT LEVEL MED")) { g_demo_control.output_gain_q15 = 16384U; console_write("OK output level MED (50%)\r\n"); return; }
  if (line_eq(line, "OUTPUT LEVEL HIGH")) { g_demo_control.output_gain_q15 = 24576U; console_write("OK output level HIGH (75%)\r\n"); return; }
  if (line_eq(line, "OUTPUT LEVEL UNITY")) { g_demo_control.output_gain_q15 = 32768U; console_write("OK output level UNITY (100% digital)\r\n"); return; }
  if (line_eq(line, "DIRECT BT LEFT") || line_eq(line, "DIRECT SAI1 LEFT")) { g_demo_control.direct_source = DEMO_DIRECT_BT; g_demo_control.direct_channel = DEMO_CHANNEL_LEFT; g_demo_control.dsp_enabled = 0U; g_demo_control.background_load_enabled = 0U; g_demo_control.stream_enabled = 0U; g_demo_control.evidence_enabled = 0U; g_demo_control.plot_enabled = 0U; g_demo_control.audio_capture_enabled = 1U; g_demo_control.output_enabled = 1U; g_demo_control.test_tone_step = 0U; output_queue_reset(); if (g_dma_started) stop_dma(); console_write("OK DIRECT BT/SAI1 LEFT: decode -> speaker only; DSP/FSM/filter bypassed\r\n"); return; }
  if (line_eq(line, "DIRECT BT RIGHT") || line_eq(line, "DIRECT SAI1 RIGHT")) { g_demo_control.direct_source = DEMO_DIRECT_BT; g_demo_control.direct_channel = DEMO_CHANNEL_RIGHT; g_demo_control.dsp_enabled = 0U; g_demo_control.background_load_enabled = 0U; g_demo_control.stream_enabled = 0U; g_demo_control.evidence_enabled = 0U; g_demo_control.plot_enabled = 0U; g_demo_control.audio_capture_enabled = 1U; g_demo_control.output_enabled = 1U; g_demo_control.test_tone_step = 0U; output_queue_reset(); if (g_dma_started) stop_dma(); console_write("OK DIRECT BT/SAI1 RIGHT: decode -> speaker only; DSP/FSM/filter bypassed\r\n"); return; }
  if (line_eq(line, "DIRECT BT MIX") || line_eq(line, "DIRECT SAI1 MIX")) { g_demo_control.direct_source = DEMO_DIRECT_BT; g_demo_control.direct_channel = DEMO_CHANNEL_MIX; g_demo_control.dsp_enabled = 0U; g_demo_control.background_load_enabled = 0U; g_demo_control.stream_enabled = 0U; g_demo_control.evidence_enabled = 0U; g_demo_control.plot_enabled = 0U; g_demo_control.audio_capture_enabled = 1U; g_demo_control.output_enabled = 1U; g_demo_control.test_tone_step = 0U; output_queue_reset(); if (g_dma_started) stop_dma(); console_write("OK DIRECT BT/SAI1 MIX: decode -> speaker only; DSP/FSM/filter bypassed\r\n"); return; }
  if (line_eq(line, "DIRECT TC LEFT") || line_eq(line, "DIRECT SAI2 LEFT")) { g_demo_control.direct_source = DEMO_DIRECT_TC; g_demo_control.direct_channel = DEMO_CHANNEL_LEFT; g_demo_control.dsp_enabled = 0U; g_demo_control.background_load_enabled = 0U; g_demo_control.stream_enabled = 0U; g_demo_control.evidence_enabled = 0U; g_demo_control.plot_enabled = 0U; g_demo_control.audio_capture_enabled = 1U; g_demo_control.output_enabled = 1U; g_demo_control.test_tone_step = 0U; output_queue_reset(); if (g_dma_started) stop_dma(); console_write("OK DIRECT TC/SAI2 LEFT: decode -> speaker only; DSP/FSM/filter bypassed\r\n"); return; }
  if (line_eq(line, "DIRECT TC RIGHT") || line_eq(line, "DIRECT SAI2 RIGHT")) { g_demo_control.direct_source = DEMO_DIRECT_TC; g_demo_control.direct_channel = DEMO_CHANNEL_RIGHT; g_demo_control.dsp_enabled = 0U; g_demo_control.background_load_enabled = 0U; g_demo_control.stream_enabled = 0U; g_demo_control.evidence_enabled = 0U; g_demo_control.plot_enabled = 0U; g_demo_control.audio_capture_enabled = 1U; g_demo_control.output_enabled = 1U; g_demo_control.test_tone_step = 0U; output_queue_reset(); if (g_dma_started) stop_dma(); console_write("OK DIRECT TC/SAI2 RIGHT: decode -> speaker only; DSP/FSM/filter bypassed\r\n"); return; }
  if (line_eq(line, "DIRECT TC MIX") || line_eq(line, "DIRECT SAI2 MIX")) { g_demo_control.direct_source = DEMO_DIRECT_TC; g_demo_control.direct_channel = DEMO_CHANNEL_MIX; g_demo_control.dsp_enabled = 0U; g_demo_control.background_load_enabled = 0U; g_demo_control.stream_enabled = 0U; g_demo_control.evidence_enabled = 0U; g_demo_control.plot_enabled = 0U; g_demo_control.audio_capture_enabled = 1U; g_demo_control.output_enabled = 1U; g_demo_control.test_tone_step = 0U; output_queue_reset(); if (g_dma_started) stop_dma(); console_write("OK DIRECT TC/SAI2 MIX: decode -> speaker only; DSP/FSM/filter bypassed\r\n"); return; }
  if (line_eq(line, "DIRECT OFF")) {
    g_demo_control.direct_source = DEMO_DIRECT_OFF;
    /* DIRECT may have started only one receiver. Restart the capture set so
     * normal AUTO mode really has both inputs available. */
    if (g_dma_started) stop_dma();
    output_queue_reset();
    console_write("OK DIRECT bypass off; normal routing restored with clean source hysteresis\r\n");
    return;
  }
  if (line_eq(line, "DIRECT STATUS")) { console_write("DIRECT source="); console_u32(g_demo_control.direct_source); console_write(" channel="); console_write(listen_channel_name(g_demo_control.direct_channel)); console_write(" pcmMode="); console_u32(g_demo_control.pcm_decode_mode); console_write(" gainQ15="); console_u32(g_demo_control.output_gain_q15); console_write(" rate="); console_write(output_clock_locked_path() ? "EXACT1:1" : "ELASTIC-SLOW"); console_write("\r\n"); return; }
  if (line_eq(line, "INTEGRITY") || line_eq(line, "INTEGRITY STATUS")) {
    uint32_t depth = output_sample_depth();
    console_write("INTEGRITY rate="); console_write(output_clock_locked_path() ? "EXACT1:1" : "ELASTIC-SLOW");
    console_write(" depthSamples="); console_u32(depth);
    console_write(" min="); console_u32(g_output_depth_min_samples == 0xFFFFFFFFUL ? 0U : g_output_depth_min_samples);
    console_write(" max="); console_u32(g_output_depth_max_samples);
    console_write(" stepQ16="); console_u32(g_output_step_q16);
    console_write(" under="); console_u32(g_demo_diag.speaker_underruns);
    console_write(" qOver="); console_u32(g_demo_diag.speaker_queue_overruns);
    console_write(" btOver="); console_u32(g_demo_diag.bt_dma_overruns);
    console_write(" tcOver="); console_u32(g_demo_diag.tc_dma_overruns);
    console_write(" sai1Err="); console_u32(g_demo_diag.sai1_errors);
    console_write(" sai2Err="); console_u32(g_demo_diag.sai2_errors);
    console_write("\r\n");
    return;
  }
  if (line_eq(line, "INTEGRITY RESET")) {
    g_demo_diag.speaker_underruns = 0U;
    g_demo_diag.speaker_queue_overruns = 0U;
    g_demo_diag.bt_dma_overruns = 0U;
    g_demo_diag.tc_dma_overruns = 0U;
    g_output_depth_min_samples = output_sample_depth();
    g_output_depth_max_samples = output_sample_depth();
    console_write("OK integrity counters reset\r\n");
    return;
  }
  if (line_eq(line, "TONE 1K")) { output_queue_reset(); g_demo_control.test_tone_step = 1U; g_demo_diag.test_tone_hz = 1000U; g_tone_phase = 0U; console_write("OK direct output tone 1 kHz\r\n"); return; }
  if (line_eq(line, "TONE 2K")) { output_queue_reset(); g_demo_control.test_tone_step = 2U; g_demo_diag.test_tone_hz = 2000U; g_tone_phase = 0U; console_write("OK direct output tone 2 kHz\r\n"); return; }
  if (line_eq(line, "TONE 3K")) { output_queue_reset(); g_demo_control.test_tone_step = 3U; g_demo_diag.test_tone_hz = 3000U; g_tone_phase = 0U; console_write("OK direct output tone 3 kHz\r\n"); return; }
  if (line_eq(line, "TONE OFF")) { g_demo_control.test_tone_step = 0U; g_demo_diag.test_tone_hz = 0U; g_tone_phase = 0U; output_queue_reset(); console_write("OK direct output tone off\r\n"); return; }
  if (line_eq(line, "CAPTURE BT")) { g_demo_control.audio_capture_enabled = 1U; raw_capture_arm(RAW_CAPTURE_SOURCE_BT); console_write("OK raw BT capture armed: 4096 stereo frames before DSP\r\n"); return; }
  if (line_eq(line, "CAPTURE TC")) { g_demo_control.audio_capture_enabled = 1U; raw_capture_arm(RAW_CAPTURE_SOURCE_TC); console_write("OK raw TC capture armed: 4096 stereo frames before DSP\r\n"); return; }
  if (line_eq(line, "CAPTURE CANCEL")) { raw_capture_arm(RAW_CAPTURE_SOURCE_NONE); console_write("OK raw capture cancelled\r\n"); return; }
  if (line_eq(line, "CAPTURE STATUS")) { console_write("CAPTURE source="); console_write(raw_capture_source_name(g_raw_capture_source)); console_write(" frames="); console_u32(g_raw_capture_frames); console_write(" ready="); console_u32(g_raw_capture_ready); console_write(" decode="); console_u32(g_demo_control.pcm_decode_mode); console_write("\r\n"); return; }
  if (line_eq(line, "CAPTURE DUMP")) { raw_capture_send(); return; }
  if (line_eq(line, "RECORD BT LEFT")) { g_demo_control.audio_capture_enabled = 1U; listen_capture_arm(RAW_CAPTURE_SOURCE_BT, DEMO_CHANNEL_LEFT); console_write("OK pre-DSP BT LEFT record armed: 16384 samples / 341 ms\r\n"); return; }
  if (line_eq(line, "RECORD BT RIGHT")) { g_demo_control.audio_capture_enabled = 1U; listen_capture_arm(RAW_CAPTURE_SOURCE_BT, DEMO_CHANNEL_RIGHT); console_write("OK pre-DSP BT RIGHT record armed: 16384 samples / 341 ms\r\n"); return; }
  if (line_eq(line, "RECORD BT MIX") || line_eq(line, "RECORD BT")) { g_demo_control.audio_capture_enabled = 1U; listen_capture_arm(RAW_CAPTURE_SOURCE_BT, DEMO_CHANNEL_MIX); console_write("OK pre-DSP BT MIX record armed: 16384 samples / 341 ms\r\n"); return; }
  if (line_eq(line, "RECORD TC LEFT")) { g_demo_control.audio_capture_enabled = 1U; listen_capture_arm(RAW_CAPTURE_SOURCE_TC, DEMO_CHANNEL_LEFT); console_write("OK pre-DSP TC LEFT record armed: 16384 samples / 341 ms\r\n"); return; }
  if (line_eq(line, "RECORD TC RIGHT")) { g_demo_control.audio_capture_enabled = 1U; listen_capture_arm(RAW_CAPTURE_SOURCE_TC, DEMO_CHANNEL_RIGHT); console_write("OK pre-DSP TC RIGHT record armed: 16384 samples / 341 ms\r\n"); return; }
  if (line_eq(line, "RECORD TC MIX") || line_eq(line, "RECORD TC")) { g_demo_control.audio_capture_enabled = 1U; listen_capture_arm(RAW_CAPTURE_SOURCE_TC, DEMO_CHANNEL_MIX); console_write("OK pre-DSP TC MIX record armed: 16384 samples / 341 ms\r\n"); return; }
  if (line_eq(line, "RECORD CANCEL")) { listen_capture_arm(RAW_CAPTURE_SOURCE_NONE, DEMO_CHANNEL_MIX); console_write("OK listen record cancelled\r\n"); return; }
  if (line_eq(line, "RECORD STATUS")) { console_write("RECORD source="); console_write(raw_capture_source_name(g_listen_capture_source)); console_write(" channel="); console_write(listen_channel_name(g_listen_capture_channel)); console_write(" samples="); console_u32(g_listen_capture_samples); console_write(" ready="); console_u32(g_listen_capture_ready); console_write(" decode="); console_u32(g_demo_control.pcm_decode_mode); console_write("\r\n"); return; }
  if (line_eq(line, "RECORD DUMP")) { listen_capture_send(); return; }
  if (line_eq(line, "PCM LOW24")) { g_demo_control.pcm_decode_mode = PCM_DECODE_LOW24; console_write("OK PCM decode LOW24 (breadboard/current default)\r\n"); return; }
  if (line_eq(line, "PCM UPPER16")) { g_demo_control.pcm_decode_mode = PCM_DECODE_UPPER16; console_write("OK PCM decode UPPER16\r\n"); return; }
  if (line_eq(line, "PCM MIDDLE16")) { g_demo_control.pcm_decode_mode = PCM_DECODE_MIDDLE16; console_write("OK PCM decode MIDDLE16\r\n"); return; }
  if (line_eq(line, "PCM LOW16")) { g_demo_control.pcm_decode_mode = PCM_DECODE_LOW16; console_write("OK PCM decode LOW16\r\n"); return; }
  if (line_eq(line, "PCM STATUS")) { console_write("PCM decode mode="); console_u32(g_demo_control.pcm_decode_mode); console_write(" (0=LOW24 1=UPPER16 2=MIDDLE16 3=LOW16)\r\n"); return; }
  if (line_eq(line, "BT MONO AUTO")) { g_demo_control.bt_mono_mode = DEMO_BT_MONO_AUTO; console_write("OK BT mono AUTO\r\n"); return; }
  if (line_eq(line, "BT MONO MIX")) { g_demo_control.bt_mono_mode = DEMO_BT_MONO_MIX; console_write("OK BT mono MIX\r\n"); return; }
  if (line_eq(line, "BT MONO LEFT")) { g_demo_control.bt_mono_mode = DEMO_BT_MONO_LEFT; console_write("OK BT mono LEFT\r\n"); return; }
  if (line_eq(line, "BT MONO RIGHT")) { g_demo_control.bt_mono_mode = DEMO_BT_MONO_RIGHT; console_write("OK BT mono RIGHT\r\n"); return; }
  if (line_eq(line, "DSP ON")) { g_demo_control.dsp_enabled = 1U; (void)AudioDynamics_Reset(&g_dynamics); console_write("OK DSP on\r\n"); return; }
  if (line_eq(line, "DSP OFF")) { g_demo_control.dsp_enabled = 0U; console_write("OK DSP bypass - routing preserved\r\n"); return; }
  if (line_eq(line, "DSP RESET")) { (void)AudioDynamics_Reset(&g_dynamics); console_write("OK DSP state reset\r\n"); return; }
  if (line_eq(line, "DSP EQ ON")) { AudioDynamics_SetResonanceEq(&g_dynamics, true); console_write("OK 1.6 kHz bone-resonance compensation on\r\n"); return; }
  if (line_eq(line, "DSP EQ OFF")) { AudioDynamics_SetResonanceEq(&g_dynamics, false); console_write("OK resonance compensation bypassed\r\n"); return; }
  if (line_eq(line, "DSP COMP ON")) { AudioDynamics_SetCompression(&g_dynamics, true); console_write("OK slow compressor on\r\n"); return; }
  if (line_eq(line, "DSP COMP OFF")) { AudioDynamics_SetCompression(&g_dynamics, false); console_write("OK compressor bypassed; filters+limiter remain\r\n"); return; }
  if (line_eq(line, "DSP STATUS")) { print_evidence(); return; }
  if (line_eq(line, "EVIDENCE ON")) { g_demo_control.evidence_enabled = 1U; console_write("OK evidence reports on\r\n"); return; }
  if (line_eq(line, "EVIDENCE OFF")) { g_demo_control.evidence_enabled = 0U; console_write("OK evidence reports off\r\n"); return; }
  if (line_eq(line, "PLOT ON")) { g_demo_control.plot_enabled = 0U; console_write("R25: legacy binary plot disabled; use !SP / REAL SCOPE instead\r\n"); return; }
  if (line_eq(line, "PLOT OFF")) { g_demo_control.plot_enabled = 0U; console_write("OK plot stream off\r\n"); return; }
  if (line_eq(line, "SYNTH ON") || line_eq(line, "SYNTH OFF") || line_eq(line, "DEMO AUTO")) {
    console_write("ERR synthetic audio and scripted FSM disabled in the final hardware demonstration\r\n");
    return;
  }
  if (line_eq(line, "BT PROFILE") || line_eq(line, "PROFILE")) { g_demo_control.apply_bt_profile_request = 1U; console_write("OK BT401 profile requested\r\n"); return; }
  if (line_eq(line, "BT QUERY")) { g_demo_control.bt_query_request = 1U; console_write("OK BT401 query requested\r\n"); return; }
  if (line_eq(line, "BTLINK AUTO")) { g_demo_control.bt_link_mode = DEMO_BTLINK_AUTO; console_write("OK BT link auto\r\n"); return; }
  if (line_eq(line, "BTLINK ON")) { g_demo_control.bt_link_mode = DEMO_BTLINK_FORCE_ON; console_write("OK BT link forced on\r\n"); return; }
  if (line_eq(line, "BTLINK OFF")) { g_demo_control.bt_link_mode = DEMO_BTLINK_FORCE_OFF; console_write("OK BT link forced off\r\n"); return; }
  if (line_eq(line, "TEST CLOCK")) { print_test_result("CLOCK", test_clock()); return; }
  if (line_eq(line, "TEST RTOS")) { print_test_result("RTOS", test_rtos()); return; }
  if (line_eq(line, "TEST DSP")) { print_test_result("DSP", test_dsp()); return; }
  if (line_eq(line, "TEST AUDIO")) { print_test_result("AUDIO", test_audio()); return; }
  if (line_eq(line, "TEST OUTPUT")) { print_test_result("OUTPUT", test_output()); return; }
  if (line_eq(line, "TEST BT")) { print_test_result("BT401", test_bt()); return; }
  if (line_eq(line, "TEST ALL")) { test_all(); return; }
  if (line_eq(line, "RESETDIAG")) { g_demo_control.reset_diagnostics_request = 1U; console_write("OK diagnostics reset requested\r\n"); return; }
  queue_protocol_command(line);
}

static void poll_console(void)
{
  uint8_t c;
  while (console_rx_pop(&c)) {
    if (c == '!') {
      /* Self-resynchronising R24 command frame.  If a previous newline was
       * lost, a new '!' discards the partial command instead of concatenating
       * two requests into one unknown string. */
      g_console_line_len = 0U;
      g_console_line[g_console_line_len++] = '!';
    } else if (c == '\r' || c == '\n') {
      if (g_console_line_len > 0U) {
        g_console_line[g_console_line_len] = '\0';
        process_console_line(g_console_line);
        g_console_line_len = 0U;
      }
    } else if (c == 0x08U || c == 0x7FU) {
      if (g_console_line_len > 0U) g_console_line_len--;
    } else if (c >= 0x20U && c <= 0x7EU) {
      if (g_console_line_len < CONSOLE_LINE_LEN - 1U) g_console_line[g_console_line_len++] = (char)c;
      else { g_console_line_len = 0U; g_demo_diag.console_commands_error++; }
    }
  }
}

void Demo_EarlyInit(void)
{
  memset((void *)&g_demo_control, 0, sizeof(g_demo_control));
  memset((void *)&g_demo_diag, 0, sizeof(g_demo_diag));
  g_demo_diag.magic = DEMO_MAGIC;
  g_demo_diag.fw_version = DEMO_FW_VERSION;
  g_demo_control.requested_source_mode = DEMO_MODE_AUTO;
  g_demo_control.bt_link_mode = DEMO_BTLINK_AUTO;
  g_demo_control.interfaces_ready = 1U;
  g_demo_control.background_load_enabled = 0U;
  g_demo_control.stream_enabled = 0U;
  g_demo_control.audio_capture_enabled = 0U;
  g_demo_control.output_enabled = 0U;
  g_demo_control.output_gain_q15 = 8192U; /* Conservative 25% digital level for first bone-conductor test. */
  g_demo_control.dsp_enabled = 0U;
  g_demo_control.evidence_enabled = 0U;
  g_demo_control.plot_enabled = 0U;
  g_demo_control.bt_mono_mode = DEMO_BT_MONO_LEFT; /* proven clean channel; AUTO can chatter between slots */
  g_demo_control.pcm_decode_mode = PCM_DECODE_LOW24;
  g_demo_control.direct_source = DEMO_DIRECT_OFF;
  g_demo_control.direct_channel = DEMO_CHANNEL_MIX;
  g_demo_control.test_tone_step = 0U;
  g_r21_auto_level_enabled = 0U;
  g_r21_smart_mono_enabled = 0U;
  g_r21_auto_gain_q15 = 32768U;
  g_r24_gain_update_div = 0U;
  g_r21_have_last_good_output = 0U;
  g_r21_conceal_streak = 0U;
  g_r21_conceal_total = 0U;
  g_r21_tx_failures = 0U;
  g_demo_control.bt_present_rms_threshold = 350U;
  g_demo_control.bt_present_pp_threshold = 900U;
  g_raw_capture_source = RAW_CAPTURE_SOURCE_NONE;
  g_raw_capture_frames = 0U;
  g_raw_capture_armed = 0U;
  g_raw_capture_ready = 0U;
  g_raw_capture_announced = 0U;
  g_listen_capture_source = RAW_CAPTURE_SOURCE_NONE;
  g_listen_capture_channel = DEMO_CHANNEL_MIX;
  g_listen_capture_samples = 0U;
  g_listen_capture_armed = 0U;
  g_listen_capture_ready = 0U;
  g_listen_capture_announced = 0U;
  Demo_SetStage(0x10U);
}

static void tune_audio_gpio_edge_rate(void)
{
  /* Match the earlier clean breadboard firmware: these audio pins do not need
   * VERY_HIGH GPIO edge speed at a few MHz. Slower edges reduce ringing/ground
   * injection into the analogue front-end without changing the SAI timing. */
  const uint32_t pb_mask = (3UL << (8U*2U)) | (3UL << (9U*2U)) | (3UL << (10U*2U)) |
                           (3UL << (12U*2U)) | (3UL << (13U*2U)) | (3UL << (15U*2U));
  const uint32_t pc_mask = (3UL << (3U*2U));
  GPIOB->OSPEEDR &= ~pb_mask;
  GPIOC->OSPEEDR &= ~pc_mask;
}

void Demo_AppInit(void)
{
  AudioDynamicsConfig_t cfg = AudioDynamics_DefaultConfig();
  GroupFeatures_Init(&g_group_features);
  GroupGuardian_Init(&g_group_guardian);
  g_group_events = xQueueCreate(8U, sizeof(GroupEvent));
  if (g_group_events == NULL) Demo_SetError(0xF231U);
  dwt_enable();
  TcCleanup_Reset(&g_r27_tc_filter);
  tune_audio_gpio_edge_rate();
  if (ConnectivityService_Init(&g_service, DEMO_AUDIO_RATE_HZ, HAL_GetTick()) != CONNECTIVITY_SERVICE_OK) {
    Demo_SetError(0xC101U);
  }
  if (AudioDynamics_Init(&g_dynamics, &cfg) != AUDIO_DYNAMICS_OK) {
    Demo_SetError(0xC102U);
  }
  refresh_clocks();
  Demo_SetStage(0x30U);
}

static void create_child_tasks(void)
{
  BaseType_t a, b, c, d, e;
  if (g_children_started) return;
  a = xTaskCreate(StartAudioTask, "AudioTask", 1024U, NULL, 7U, &g_audio_task);
  b = xTaskCreate(StartBtTask, "BtTask", 512U, NULL, 5U, &g_bt_task);
  c = xTaskCreate(StartBackgroundTask, "Background", 384U, NULL, 2U, &g_background_task);
  d = xTaskCreate(StartCpuLoadB, "CPULoadB", 256U, NULL, 2U, &g_cpu_load_b_task);
  e = xTaskCreate(StartHighPriorityLoad, "HighLoad7", 256U, NULL, 7U, &g_r32_hi_task);
  if (a != pdPASS) Demo_SetError(0xF201U);
  if (b != pdPASS) Demo_SetError(0xF202U);
  if (c != pdPASS) Demo_SetError(0xF203U);
  if (d != pdPASS) console_write("WARN: CPULoadB creation failed (insufficient FreeRTOS heap); audio tasks remain available\r\n");
  if (e != pdPASS) console_write("WARN: HighLoad7 creation failed (insufficient FreeRTOS heap); !RH unavailable\r\n");
  g_children_started = (a == pdPASS && b == pdPASS && c == pdPASS) ? 1U : 0U;
  console_write("RTOS: children Audio/BT/Background/CPULoadB/HighLoad7 requested after scheduler start\r\n");
}

void Demo_ControlTask(void *argument)
{
  (void)argument;
  Demo_SetStage(0x50U);
  vTaskPrioritySet(NULL, 6U); /* R26: responsive control plane below audio, above BT/background. */
  console_write("\r\nRTOS: scheduler running; ControlTask entered\r\n");
  start_uart_rx_interrupts();
  if (g_demo_diag.console_rx_irq_armed) console_write("RX: USART2 interrupt receiver ARMED - commands are live\r\n");
  else console_write("RX: USART2 interrupt receiver FAILED - check err code\r\n");
  console_write("DHA RUBRIC DEMO READY - USART2 via NUCLEO ST-LINK VCP\r\n");
  console_write("261013 CONNECTIVITY: real DMA/state-machine evidence; use !FA !FV !RM\r\n");
  console_write("GROUP G1: B1 short=profile; long=auto; !GP reports real group feature state; default OFF\r\n");
  console_write("Type HELP. Start with PING then TEST ALL.\r\n");
  create_child_tasks();
  group_button_init();
  print_clock();
  g_last_stream_ms = HAL_GetTick();

  for (;;) {
    uint32_t now = HAL_GetTick();
    poll_console();
    group_button_poll(now);
    update_task_rates(now);
    if (g_raw_capture_ready && !g_raw_capture_announced) {
      g_raw_capture_announced = 1U;
      console_write("CAPTURE READY source="); console_write(raw_capture_source_name(g_raw_capture_source));
      console_write(" frames="); console_u32(g_raw_capture_frames);
      console_write(" - send CAPTURE DUMP\r\n");
    }
    if (g_listen_capture_ready && !g_listen_capture_announced) {
      g_listen_capture_announced = 1U;
      console_write("RECORD READY source="); console_write(raw_capture_source_name(g_listen_capture_source));
      console_write(" channel="); console_write(listen_channel_name(g_listen_capture_channel));
      console_write(" samples="); console_u32(g_listen_capture_samples);
      console_write(" - send RECORD DUMP\r\n");
    }
    if (g_demo_control.evidence_enabled && (uint32_t)(now - g_last_evidence_report_ms) >= 1000U) {
      g_last_evidence_report_ms = now;
      print_evidence();
    }
    /* R25: do NOT call send_plot_frame().  The old A5 5A HD binary
     * stream was the source of the visible garbage bytes and could desynchronise
     * dashboard command/response traffic.  Scope is explicit ASCII via !SP. */
    if (g_demo_control.stream_enabled && (uint32_t)(now - g_last_stream_ms) >= 500U) {
      g_last_stream_ms = now;
      print_status();
    }
    g_demo_diag.control_task_loops++;
    g_demo_diag.control_stack_high_water = (uint32_t)uxTaskGetStackHighWaterMark(NULL);
    r29_refresh_debug_snapshot(now);
    g_demo_diag.uptime_ms = now;
    vTaskDelay(pdMS_TO_TICKS(5U));
  }
}

static void StartAudioTask(void *argument)
{
  (void)argument;
  for (;;) {
    uint32_t now = HAL_GetTick();
    group_audio_update();
    if (g_r28_route_restart_pending) {
      /* One clean application FSM session. No simulated packets/link bits.
       * Input validity must be rebuilt from real PCM after both RX DMAs start. */
      g_r28_route_restart_pending = 0U;
      if (g_dma_started) stop_dma();
      g_pending_command_plus1 = 0U; /* discard an old direct-mode command */
      g_bt_present_streak = 0U;
      g_bt_absent_streak = 0U;
      g_demo_diag.bt_audio_present = 0U;
      g_demo_diag.tc_valid = 0U;
      if (ConnectivityService_Init(&g_service, DEMO_AUDIO_RATE_HZ, now) != CONNECTIVITY_SERVICE_OK) {
        Demo_SetError(0xC103U);
      } else {
        g_last_requested_mode = UINT32_MAX; /* reapply actual both-enabled source policy */
        g_r28_route_epoch++;
      }
    }
    if (g_demo_control.direct_source == DEMO_DIRECT_OFF) {
      apply_requested_mode(now);
      apply_pending_command(now);
    }

    if (g_demo_control.audio_capture_enabled && !g_dma_started) {
      refresh_clocks();
      if ((g_demo_diag.clock_warning_flags & 0x0CU) == 0U) {
        HAL_StatusTypeDef st = start_dma();
        if (st != HAL_OK) Demo_SetError(0xA100U | ((uint32_t)st & 0xFU));
      } else {
        Demo_SetError(0xA180U);
      }
    } else if (!g_demo_control.audio_capture_enabled && g_dma_started) {
      stop_dma();
    }

    if (g_demo_control.direct_source == DEMO_DIRECT_OFF) {
      if ((uint32_t)(now - g_last_state_step_ms) >= 20U) {
        step_service(now);
        g_last_state_step_ms = now;
      }

    }

    /* Drain immutable RX snapshots. The DMA ISR copied these blocks before the
     * circular DMA could reuse the memory, so block identity cannot be lost even
     * if this task is briefly delayed by diagnostics or another RTOS task. */
    {
      uint32_t handled = 0U;
      while (handled < (CAPTURE_RING_DEPTH - 1U) && capture_pop_bt(g_bt_work_words)) {
        process_bt_words(g_bt_work_words, HAL_GetTick());
        handled++;
      }
      handled = 0U;
      while (handled < (CAPTURE_RING_DEPTH - 1U) && capture_pop_tc(g_tc_work_words)) {
        process_tc_words(g_tc_work_words, HAL_GetTick());
        handled++;
      }
    }

    if (g_demo_control.reset_diagnostics_request) {
      (void)ConnectivityService_ResetDiagnostics(&g_service);
      g_demo_diag.bt_blocks_processed = g_demo_diag.tc_blocks_processed = 0U;
      g_demo_diag.bt_dma_overruns = g_demo_diag.tc_dma_overruns = 0U;
      g_demo_diag.speaker_underruns = g_demo_diag.speaker_queue_overruns = 0U;
      g_demo_diag.speaker_refill_misses = 0U;
      g_demo_diag.dsp_max_us = g_demo_diag.dsp_limited_samples = 0U;
      g_demo_diag.service_deadline_misses = 0U;
      g_demo_control.reset_diagnostics_request = 0U;
    }
    g_demo_diag.audio_task_loops++;
    g_demo_diag.audio_stack_high_water = (uint32_t)uxTaskGetStackHighWaterMark(NULL);
    /* If capture data remains, yield without sleeping so the task can catch up;
     * otherwise block for one tick and leave CPU time for control/UI tasks. */
    if ((g_bt_cap_head != g_bt_cap_tail) || (g_tc_cap_head != g_tc_cap_tail)) {
      taskYIELD();
    } else {
      vTaskDelay(pdMS_TO_TICKS(1U));
    }
  }
}

static void StartBtTask(void *argument)
{
  (void)argument;
  for (;;) {
    poll_bt_uart();
    if (g_demo_control.apply_bt_profile_request) {
      BtProfileConfig_t cfg = BtProfile_DefaultConfig();
      size_t sent = 0U;
      BtProfileResult_t r;
      g_demo_diag.bt_profile_attempts++;
      r = BtProfile_Apply(&cfg, bt_tx, bt_delay, NULL, &sent);
      g_demo_diag.bt_profile_commands_sent = (uint32_t)sent;
      if (r == BT_PROFILE_OK) g_demo_diag.bt_profile_successes++;
      g_demo_control.apply_bt_profile_request = 0U;
    }
    if (g_demo_control.bt_query_request) {
      bt_query();
      g_demo_control.bt_query_request = 0U;
    }
    g_demo_diag.bt_task_loops++;
    g_demo_diag.bt_stack_high_water = (uint32_t)uxTaskGetStackHighWaterMark(NULL);
    vTaskDelay(pdMS_TO_TICKS(10U));
  }
}

/* Saturation demonstration: LOW-priority task consumes spare processor time
 * while AudioTask/ControlTask/BtTask continue to preempt it. TIM2 RTOS
 * runtime accounting, not a hardcoded CPU number, determines measured load.
 * The 60 s hardware-tick watchdog automatically restores ordinary idle time.
 * Mode 0 = OFF, 1 = light load (!RA), 2 = SATURATION (!RS).
 */

static void StartBackgroundTask(void *argument)
{
  volatile uint32_t x = 0x13579BDFU;
  uint32_t next_idle_slice_ms = 0U;
  (void)argument;
  for (;;) {
    uint32_t load = g_demo_control.background_load_enabled;
    if (load == 2U) {
      uint32_t now = HAL_GetTick();
      /* Signed subtraction handles 32-bit HAL tick wrap. In particular,
       * a paused/stopped console cannot leave the CPU pegged forever. */
      if ((int32_t)(now - g_stress_stop_at_ms) >= 0) {
        g_demo_control.background_load_enabled = 0U;
        g_r31_dual_load = 0U;
        next_idle_slice_ms = 0U;
        continue;
      }
      /* A real non-optimizable integer workload; no fake loop counts.
       * Keep the task READY most of the time: lower priority than all
       * production tasks, so their DMA-induced work preempts it.
       */
      for (uint32_t i = 0U; i < 2048U; ++i)
        x = (x << 5) ^ (x >> 3) ^ (i + 0x9E3779B9U);
      g_stress_checksum = x;
      g_demo_diag.background_work_iterations++;
      g_demo_diag.background_task_loops++;
      g_demo_diag.background_stack_high_water = (uint32_t)uxTaskGetStackHighWaterMark(NULL);
      /* Short idle opportunity at 100 ms intervals for kernel housekeeping.
       * The 1 ms delay is not proof of CPU %; the TIM2 profiler is.
       */
      if ((int32_t)(now - next_idle_slice_ms) >= 0) {
        next_idle_slice_ms = now + 100U;
        vTaskDelay(pdMS_TO_TICKS(1U));
      }
      continue;
    }
    next_idle_slice_ms = 0U;
    if (load == 1U) {
      for (uint32_t i = 0U; i < 4000U; ++i)
        x = (x << 5) ^ (x >> 3) ^ (i + 0x9E3779B9U);
      g_stress_checksum = x;
      g_demo_diag.background_work_iterations++;
      vTaskDelay(pdMS_TO_TICKS(1U));
    } else {
      vTaskDelay(pdMS_TO_TICKS(50U));
    }
    g_demo_diag.background_task_loops++;
    g_demo_diag.background_stack_high_water = (uint32_t)uxTaskGetStackHighWaterMark(NULL);
  }
}

/* Second, independent CPU-bound worker with exactly the SAME priority as
 * Background.  In !RD mode both remain READY, and FreeRTOS time slicing lets
 * them alternate.  There is only ONE executing task at any instant (single
 * core); progress in both counters + measured runtime proves concurrency. */
static void StartCpuLoadB(void *argument)
{
  volatile uint32_t x = 0x2468ACE1U;
  uint32_t next_idle_slice_ms = 0U;
  (void)argument;
  for (;;) {
    if ((g_demo_control.background_load_enabled == 2U) && g_r31_dual_load) {
      const uint32_t now = HAL_GetTick();
      if ((int32_t)(now - g_stress_stop_at_ms) >= 0) {
        g_r31_dual_load = 0U;
        g_demo_control.background_load_enabled = 0U;
        next_idle_slice_ms = 0U;
        continue;
      }
      for (uint32_t i=0U; i<2048U; ++i)
        x = x * 1664525U + 1013904223U + i;
      g_r31_cpu_b_checksum = x;
      g_r31_cpu_b_work_blocks++;
      g_r31_cpu_b_loops++;
      if ((int32_t)(now - next_idle_slice_ms) >= 0) {
        next_idle_slice_ms = now + 100U;
        vTaskDelay(pdMS_TO_TICKS(1U));
      }
      continue;
    }
    next_idle_slice_ms = 0U;
    g_r31_cpu_b_loops++;
    vTaskDelay(pdMS_TO_TICKS(50U));
  }
}

/* R34: optional STRICT priority-7 contention experiment, NOT production code.
 * Unlike the R33 burst implementation, this is a true continuous CPU spin:
 *       NO vTaskDelay(), taskYIELD(), osDelay(), or yield inside active loop.
 * FreeRTOS may preempt this task to run AudioTask (also priority 7), but a
 * priority-6 ControlTask cannot execute while this thread is always READY.
 * We cap the experiment at 20 seconds from TIM6 HAL tick, independent of
 * UART commands, as a recovery mechanism that does NOT sleep during load.
 * Never use this in the actual hearing-aid operational mode. The speaker
 * may glitch and UART/status commands can queue up until completion.
 */
static void StartHighPriorityLoad(void *argument)
{
  uint32_t x = 0xBADC0FFEU;
  (void)argument;
  for (;;) {
    if (g_r32_hi_active == 0U) {
      /* Normal dormant state uses no CPU. ONLY the active mode spins. */
      vTaskDelay(pdMS_TO_TICKS(50U));
      continue;
    }
    /* Priority-7 CPU saturation: one continuous READY interval, no sleep.
     * If ControlTask starves, the hardware-timer deadline still expires.
     * Reading HAL_GetTick() is cheap; do not poll the slow UART here. */
    while (g_r32_hi_active != 0U &&
           (int32_t)(HAL_GetTick() - g_r32_hi_deadline_ms) < 0) {
      for (uint32_t i = 0U; i < 128U; ++i) {
        x ^= x << 13;
        x ^= x >> 17;
        x ^= x << 5;
      }
      g_r32_hi_work_blocks++;
      /* Measured progress is observable directly in CubeIDE Expressions.
       * Volatile checksum ensures this arithmetic is retained by the compiler. */
      if ((g_r32_hi_work_blocks & 63U) == 0U) {
        g_r32_hi_checksum = x;
        g_r32_hi_loops++;
      }
    }
    g_r32_hi_checksum = x;
    g_r32_hi_loops++;
    g_r32_hi_active = 0U;
  }
}

void HAL_UART_RxCpltCallback(UART_HandleTypeDef *huart)
{
  if (huart == NULL) return;

  if (huart->Instance == USART2) {
    uint16_t head = g_console_rx_head;
    uint16_t next = (uint16_t)((head + 1U) & (UART_RX_RING_SIZE - 1U));
    if (next == g_console_rx_tail) {
      g_demo_diag.console_rx_overruns++;
    } else {
      g_console_rx_ring[head] = g_console_rx_irq_byte;
      g_console_rx_head = next;
      g_demo_diag.console_rx_bytes++;
    }
    if (HAL_UART_Receive_IT(&huart2, &g_console_rx_irq_byte, 1U) != HAL_OK) {
      g_demo_diag.console_rx_irq_armed = 0U;
      g_demo_diag.console_uart_errors++;
    }
  } else if (huart->Instance == USART1) {
    uint16_t head = g_bt_rx_head;
    uint16_t next = (uint16_t)((head + 1U) & (UART_RX_RING_SIZE - 1U));
    if (next == g_bt_rx_tail) {
      g_demo_diag.bt_uart_overruns++;
    } else {
      g_bt_rx_ring[head] = g_bt_rx_irq_byte;
      g_bt_rx_head = next;
      g_demo_diag.bt_uart_rx_bytes++;
    }
    if (HAL_UART_Receive_IT(&huart1, &g_bt_rx_irq_byte, 1U) != HAL_OK) {
      g_demo_diag.bt_rx_irq_armed = 0U;
      g_demo_diag.bt_uart_errors++;
    }
  }
}

void HAL_UART_ErrorCallback(UART_HandleTypeDef *huart)
{
  if (huart == NULL) return;
  if (huart->Instance == USART2) {
    g_demo_diag.console_uart_errors++;
    __HAL_UART_CLEAR_FLAG(&huart2, UART_CLEAR_PEF | UART_CLEAR_FEF | UART_CLEAR_NEF | UART_CLEAR_OREF);
    if (HAL_UART_Receive_IT(&huart2, &g_console_rx_irq_byte, 1U) == HAL_OK)
      g_demo_diag.console_rx_irq_armed = 1U;
  } else if (huart->Instance == USART1) {
    g_demo_diag.bt_uart_errors++;
    __HAL_UART_CLEAR_FLAG(&huart1, UART_CLEAR_PEF | UART_CLEAR_FEF | UART_CLEAR_NEF | UART_CLEAR_OREF);
    if (HAL_UART_Receive_IT(&huart1, &g_bt_rx_irq_byte, 1U) == HAL_OK)
      g_demo_diag.bt_rx_irq_armed = 1U;
  }
}

void HAL_SAI_RxHalfCpltCallback(SAI_HandleTypeDef *hsai)
{
  if (hsai == NULL) return;
  if (hsai->Instance == SAI1_Block_A) {
    g_demo_diag.bt_half_callbacks++;
    if (g_demo_control.direct_source != DEMO_DIRECT_TC)
      capture_push_bt_from_isr(&g_bt_dma_words[0]);
  } else if (hsai->Instance == SAI2_Block_A) {
    g_demo_diag.tc_half_callbacks++;
    capture_push_tc_from_isr(&g_tc_dma_words[0]);
  }
}

void HAL_SAI_RxCpltCallback(SAI_HandleTypeDef *hsai)
{
  if (hsai == NULL) return;
  if (hsai->Instance == SAI1_Block_A) {
    g_demo_diag.bt_full_callbacks++;
    if (g_demo_control.direct_source != DEMO_DIRECT_TC)
      capture_push_bt_from_isr(&g_bt_dma_words[DEMO_HALF_WORDS]);
  } else if (hsai->Instance == SAI2_Block_A) {
    g_demo_diag.tc_full_callbacks++;
    capture_push_tc_from_isr(&g_tc_dma_words[DEMO_HALF_WORDS]);
  }
}

void HAL_SAI_TxHalfCpltCallback(SAI_HandleTypeDef *hsai)
{
  if (hsai != NULL && hsai->Instance == SAI2_Block_B) {
    g_demo_diag.speaker_half_callbacks++;
    speaker_fill_region_isr(0U);
  }
}

void HAL_SAI_TxCpltCallback(SAI_HandleTypeDef *hsai)
{
  if (hsai != NULL && hsai->Instance == SAI2_Block_B) {
    g_demo_diag.speaker_full_callbacks++;
    speaker_fill_region_isr(DEMO_OUTPUT_HALF_SAMPLES);
  }
}

void HAL_SAI_ErrorCallback(SAI_HandleTypeDef *hsai)
{
  if (hsai->Instance == SAI1_Block_A) {
    g_demo_diag.sai1_errors++;
    g_demo_diag.last_sai1_error = hsai->ErrorCode;
  } else if (hsai->Instance == SAI2_Block_A || hsai->Instance == SAI2_Block_B) {
    g_demo_diag.sai2_errors++;
    g_demo_diag.last_sai2_error = hsai->ErrorCode;
  }
}

void Demo_FreeRTOSAssert(const char *file, uint32_t line)
{
  (void)file;
  Demo_SetError(0xF0A0U | (line & 0x1FU));
  Demo_ConsolePanic("FREERTOS_ASSERT", g_demo_diag.error_code);
  taskDISABLE_INTERRUPTS();
  for (;;) { }
}

void vApplicationMallocFailedHook(void)
{
  Demo_SetError(0xF001U);
  Demo_ConsolePanic("MALLOC_FAILED", 0xF001U);
  taskDISABLE_INTERRUPTS();
  for (;;) { }
}

void vApplicationStackOverflowHook(TaskHandle_t task, char *name)
{
  (void)task; (void)name;
  Demo_SetError(0xF002U);
  Demo_ConsolePanic("STACK_OVERFLOW", 0xF002U);
  taskDISABLE_INTERRUPTS();
  for (;;) { }
}
