#include "demo_app.h"
#include "sai.h"
#include "speaker_output.h"
#include "usart.h"
#include "connectivity_service.h"
#include "control_protocol.h"
#include "pcm_transport.h"
#include "tc_s24_recovery.h"
#include "audio_processing.h"
#include "audio_dynamics.h"
#include "bt_profile.h"
#include "tc_cleanup.h"
#include "tc_hum_cal.h"
#include "g1_boot_diag.h"
#include "connectivity_demo_debug.h"
#include "rtos_trace_proof.h"
#include "rtos_tick_proof.h"
#include "rtos_stats_timer.h"
#include "group_features.h"
#include "group_guardian.h"
#include "group_ui.h"
#include "dha_build_mode.h"
#include "g1_product_board.h"
#include "adaptive_preferences.h"
#include "g1_user_controls.h"
#include "dha_feedback.h"
#include "dha_effects.h"
#include "qr_demo_test.h"
#include "qr_bt_volume.h"
#include "qr_sleep_policy.h"
#include "g1_operating_config.h"
#include "g1_ambient_dsp.h"
#include "g1_scene_ml.h"
/* FreeRTOS.h defines kernel types and port macros consumed by queue.h/task.h.
 * Include it first: reversing this order causes a cascade of parser errors. */
#include "FreeRTOS.h"
#include "task.h"
#include "queue.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>


/* R20: Boot diagnostics are owned by this translation unit: the BOOT STATUS
 * command and the reporting function are always compiled and linked together.
 * main.c passes its reset/clock snapshot to this module during cold startup. */
static struct {
  uint32_t reset_csr, debug_at_reset, fallback, hse_attempt, sysclk_hz;
} s_g1_boot_snapshot;
void G1_BootCapture(uint32_t reset_csr, uint32_t debug_at_reset,
                    uint32_t fallback, uint32_t hse_attempt, uint32_t sysclk_hz)
{
  s_g1_boot_snapshot.reset_csr=reset_csr;
  s_g1_boot_snapshot.debug_at_reset=debug_at_reset;
  s_g1_boot_snapshot.fallback=fallback;
  s_g1_boot_snapshot.hse_attempt=hse_attempt;
  s_g1_boot_snapshot.sysclk_hz=sysclk_hz;
}

void G1_BootReport(void)
{
  char message[296];
  (void)snprintf(message,sizeof(message),
    "G1|BOOT|reset_csr=0x%08lX|bor=%lu|pin=%lu|lpwr=%lu|soft=%lu|iwdg=%lu|wwdg=%lu|debug_at_reset=%lu|clock=%s|hse_attempt=%lu|sysclk=%lu|nrst=%s\r\n",
    (unsigned long)s_g1_boot_snapshot.reset_csr,
    (unsigned long)((s_g1_boot_snapshot.reset_csr & RCC_CSR_BORRSTF)!=0U),
    (unsigned long)((s_g1_boot_snapshot.reset_csr & RCC_CSR_PINRSTF)!=0U),
    (unsigned long)((s_g1_boot_snapshot.reset_csr & RCC_CSR_LPWRRSTF)!=0U),
    (unsigned long)((s_g1_boot_snapshot.reset_csr & RCC_CSR_SFTRSTF)!=0U),
    (unsigned long)((s_g1_boot_snapshot.reset_csr & RCC_CSR_IWDGRSTF)!=0U),
    (unsigned long)((s_g1_boot_snapshot.reset_csr & RCC_CSR_WWDGRSTF)!=0U),
    (unsigned long)s_g1_boot_snapshot.debug_at_reset,
    s_g1_boot_snapshot.fallback?"HSI16_FALLBACK":"HSE16",
    (unsigned long)s_g1_boot_snapshot.hse_attempt,
    (unsigned long)s_g1_boot_snapshot.sysclk_hz,
    "measure_3V3_NRESET_BOOTO");
  Demo_ConsoleEarly(message);
}

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
               GROUP_EVENT_AUTO=3, GROUP_EVENT_SOURCE=4 } GroupEventType;
typedef struct { uint8_t type, value; } GroupEvent;
/* External debugger-visible telemetry, AudioTask writer. */
typedef struct {
    volatile uint32_t enabled, automatic, profile, scene, scene_updates;
    volatile uint32_t gain_q15, input_mean_abs, processed_blocks, clipped;
    volatile uint32_t event_count, event_drops, button_presses;
    volatile uint32_t guardian_level, guardian_events, guardian_recoveries;
    volatile uint32_t physical_gestures, ui_source_changes;
} GroupTelemetry;
__attribute__((used)) volatile GroupTelemetry g_group_telemetry;
static uint32_t g_group_events_seen;
static uint32_t g_group_button_seen __attribute__((unused));
static GroupUiButton g_group_ui;
/* Explicit-feedback adaptive gain; context is updated by AudioTask.
 * State declarations precede DSP functions that read the context. */
static G1AdaptivePreferences s_product_preferences;
static G1UiState s_product_ui;
/* ControlTask owns logical sleep and touch gestures. AudioTask owns all DMA.
 * Bluetooth UART TX remains exclusively in BtTask. */
static QrSleepPolicy s_qr_sleep;
static volatile uint8_t s_audio_sleep_active; /* ISR-visible sleep fence */
static volatile uint8_t s_bt_sleep_transition; /* 1=sleep, 2=wake */
static uint8_t s_test_saved_muted;
static uint8_t s_test_saved_source;
static uint8_t s_test_saved_valid;
static QrDemoTest s_test_generator;
/* BT401: request discoverability from the BT task (the sole UART1 transmitter). */
static volatile uint8_t s_bt_pair_request;
static volatile uint8_t s_bt_recover_request;
static volatile uint8_t s_mic_reinit_request;
static uint32_t s_bt_pair_tx_ms;
static uint32_t s_bt_last_rx_ms;
static volatile uint32_t s_bt_status_code;  /* 0=unknown, 1=waiting, 2=connected, 3=playing */
static uint32_t s_bt_pair_check_due;
static uint32_t s_mic_next_start_ms;

static uint8_t s_r10_watch_enabled = 0U; /* R24: no unsolicited serial traffic */
static uint32_t s_r10_watch_last_ms;
static volatile uint8_t s_r10_bt_at_pending;
static char s_r10_bt_at_line[88];
static volatile uint8_t s_pref_amp, s_pref_spectrum;
static uint8_t s_pref_enabled;
static uint32_t s_user_gain_q15=G1_UI_MAX_GAIN_Q15;
static uint32_t s_last_observed_ms;
/* R26 ambient DSP: high sensitivity without hard-gating distant activity.
 * AudioTask owns all DSP state; neither the encoder task nor an IRQ mutates it.
 * SceneML learns unlabeled acoustic clusters online. The separately bounded
 * preference learner updates only following explicit user volume feedback. */
static G1AmbientDsp s_ambient;
static uint32_t s_mic_agc_q12=4096U;
static uint32_t s_mic_noise_rms=16U;
static uint32_t s_mic_signal_rms;
static uint32_t s_mic_agc_limited;
static uint32_t s_mic_noise_attenuated;
static uint32_t s_mic_speech_likely;
static void product_mic_condition(int16_t *p)
{
  G1Ambient_Process(&s_ambient,p,p,DEMO_FRAMES_PER_HALF);
  s_mic_agc_q12=s_ambient.gain_q12;
  s_mic_noise_rms=s_ambient.ambient_floor_rms;
  s_mic_signal_rms=s_ambient.input_rms;
  s_mic_agc_limited=s_ambient.limited_samples;
  s_mic_noise_attenuated=0U; /* R26 ambient mode: NEVER gate traffic or distant speech */
  /* Heuristic only; no trained speech recognition data are available. */
  s_mic_speech_likely=(s_ambient.zero_crossings>=2U &&
      s_ambient.zero_crossings<=48U &&
      s_ambient.input_rms>s_ambient.ambient_floor_rms*2U)?1U:0U;
  /* Context uses PRE-amplification acoustic features, so the preference model
   * does not confuse an AGC-boosted quiet environment with a loud scene. */
  s_pref_amp=s_ambient.amplitude_bin;
  s_pref_spectrum=s_ambient.spectrum_bin;
}

static uint32_t g_group_ui_source_events;
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
/* R26.3: the working R34 demonstration uses LOW24 SAI word decoding.
 * Start with TC cleanup/notch disabled so any periodic artifact can be
 * diagnosed before imposing frequency-dependent processing. The previous
 * LOW16 interpretation and optional cleanup remain available for A/B tests. */
static volatile uint8_t g_r27_tc_clean_enabled=1U; /* R26.6 gentle, deterministic HP/LP */
static volatile uint8_t s_tc_notch_auto_enabled=0U; /* no auto tracking; learned narrow notch only */
static volatile uint8_t s_tc_filter_reset_requested; /* AudioTask-owned reset */


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
 * 1024 stereo frames = 8192 bytes. The 8 KiB SRAM2 scratch is not used by RTOS. */
/* SRAM2 is partitioned in the linker: 24 KiB RTOS + 8 KiB exclusive
 * diagnostic scratch. A raw recorder MUST NEVER alias the live RTOS heap. */
/* Linker-defined start of the 8 KiB SRAM2 diagnostic scratch.
 * In C this MUST be an array of unknown bound, not a uint8_t object:
 * the latter is one byte in the compiler's object-size analysis and causes
 * GCC 13 -O2 -Warray-bounds/-Wstringop-overflow false positives. Capacity
 * and lifetime are enforced by the linker region and the mutually-exclusive
 * raw/listen capture state machine. */
extern uint8_t _capture_ram2_start[];
_Static_assert(DEMO_RAW_CAPTURE_WORDS * sizeof(uint32_t) <= 8192U, "raw recorder exceeds SRAM2 scratch");
_Static_assert(DEMO_LISTEN_CAPTURE_SAMPLES * sizeof(int16_t) <= 8192U, "listen recorder exceeds SRAM2 scratch");
#define RAW_CAPTURE_SOURCE_NONE 0U
#define RAW_CAPTURE_SOURCE_BT   1U
#define RAW_CAPTURE_SOURCE_TC   2U
static uint32_t *const g_raw_capture_words = (uint32_t *)(void *)_capture_ram2_start;
static volatile uint32_t g_raw_capture_source;
static volatile uint32_t g_raw_capture_frames;
static volatile uint8_t g_raw_capture_armed;
static volatile uint8_t g_raw_capture_ready;
static uint8_t g_raw_capture_announced;

/* R9 listen recorder. Uses the same SRAM2 bank as raw capture, but stores
 * decoded PRE-DSP mono int16 PCM. 4096 samples = 85.3 ms at 48 kHz: long
 * enough to hear tone quality and short speech syllables on the PC. Raw and
 * listen capture are mutually exclusive by construction. */
static int16_t *const g_listen_capture_pcm = (int16_t *)(void *)_capture_ram2_start;
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
void Demo_NotifyMicReadyFromISR(void)
{
  if (g_audio_task != NULL) {
    BaseType_t higher = pdFALSE;
    vTaskNotifyGiveFromISR(g_audio_task, &higher);
    portYIELD_FROM_ISR(higher);
  }
}

static TaskHandle_t g_bt_task;
static TaskHandle_t g_background_task;
static TaskHandle_t g_cpu_load_b_task;
static TaskHandle_t g_r32_hi_task;
static uint8_t g_children_started;
static uint8_t g_dma_started;
static uint8_t g_bt_dma_started;
static uint8_t g_tc_dma_started;
/* Requested DMA input topology. Rebuild only on genuine topology change;
 * the previous g_dma_started guard kept MIC-only speaker DMA alive but never
 * started SAI2A when the user subsequently selected telecoil. */
static uint32_t g_dma_topology_mask;
static volatile uint8_t s_tc_restart_requested;
static volatile uint32_t s_tc_last_left_rms;
static volatile uint32_t s_tc_last_right_rms;
static volatile uint32_t s_tc_last_start_status;
/* R22: independent telecoil raw PCM diagnostics.  These values come ONLY from
 * completed SAI2A DMA snapshots; no fake input, detection threshold, or gain.
 * A silent ADC can therefore be distinguished from controller muting.
 * Probe analysis is decimated to every 32 completed 128-frame blocks. */
/* R23: hardware-confirmed on 03 Nov 2026: TC SAI2A returns right-justified
 * signed 16-bit samples in bits [15:0] of 32-bit DMA containers. R22's
 * LOW24 default reduced a real ~850-count signal to RMS ~3; LOW16 outputs
 * the user's verified 2-kHz waveform. This is TC-ONLY; BT retains LOW24. */
static volatile uint32_t s_tc_pcm_decode_mode = PCM_DECODE_LOW24; /* R34 default on SAI2A */
/* R26.4: full-precision LOW24 with explicit output-level recovery.  Only
 * AudioTask owns this state; ControlTask publishes simple requests. */
static TcS24Recovery s_tc24_level;
static volatile uint8_t s_tc24_level_enabled = 1U;
static volatile uint8_t s_tc24_level_reset_requested;
static volatile uint32_t s_tc24_level_cap_q16 = 4U * 65536U;

/* R26.6 deliberate noise-only calibration: no movement of the notch during
 * ordinary TC playback. The RTC/ControlTask analyser never reads live DMA. */
static TcHumCal s_tc_hum_cal;
static volatile uint32_t s_tc_hum_apply_hz=UINT32_MAX;
static volatile uint32_t s_tc_hum_last_hz=0U;
static volatile uint32_t s_tc_channel_mode; /* 0=auto,1=left,2=right,3=mix */
/* Avoid block-to-block channel hunting on noisy left/right telecoil PCM. */
static volatile uint8_t s_tc_use_elastic_output=0U; /* bench comparison; default exact */
static uint32_t s_tc_auto_channel=3U;
static uint32_t s_tc_candidate_channel=3U;
static uint16_t s_tc_candidate_blocks;
static volatile uint32_t s_tc_probe_blocks;
static volatile uint32_t s_tc_probe_nonzero;
static volatile uint32_t s_tc_probe_varying;
static volatile uint32_t s_tc_probe_or;
static volatile uint32_t s_tc_probe_and;
static volatile uint32_t s_tc_probe_first[4];
static volatile uint32_t s_tc_probe_rms[4][2];
static volatile uint32_t s_tc_probe_last_ms;
static volatile uint32_t s_tc_probe_hi_nonzero;
static volatile uint32_t s_tc_probe_hi_ones;
static volatile uint32_t s_tc_probe_low16_big_jumps;
static volatile uint32_t s_tc_manual_pcm_blocks;

static volatile uint32_t s_bt_last_start_status;
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
static void __attribute__((unused)) StartBackgroundTask(void *argument);
static void __attribute__((unused)) StartCpuLoadB(void *argument);
static void __attribute__((unused)) StartHighPriorityLoad(void *argument);
static const char *source_name(uint32_t s);
static void metrics_s16(const int16_t *x, size_t n, uint32_t *rms, uint32_t *pp, uint32_t *peak);
static uint32_t isqrt_u64(uint64_t x);
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
  /* DMA IRQ priority 5 is within configured FreeRTOS syscall ceiling.
   * Notify AFTER publishing completed PCM snapshot. */
  if (g_audio_task != NULL) {
    BaseType_t higher=pdFALSE;
    vTaskNotifyGiveFromISR(g_audio_task,&higher);
    portYIELD_FROM_ISR(higher);
  }
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
  /* DMA IRQ priority 5 is within configured FreeRTOS syscall ceiling.
   * Notify AFTER publishing completed PCM snapshot. */
  if (g_audio_task != NULL) {
    BaseType_t higher=pdFALSE;
    vTaskNotifyGiveFromISR(g_audio_task,&higher);
    portYIELD_FROM_ISR(higher);
  }
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

  /* USART2 is a development-only diagnostic port. The finished PCB must
   * capture/play audio with no ST-LINK, PC, serial monitor or UART cable.
   * An unavailable/broken console is not a fatal product boot failure. */
#if G1_DIAGNOSTICS_UART_ENABLE
  if (huart2.Instance == USART2 && (USART2->CR1 & USART_CR1_UE) != 0U) {
    a = HAL_UART_Receive_IT(&huart2, &g_console_rx_irq_byte, 1U);
    g_demo_diag.console_rx_irq_armed = (uint32_t)(a == HAL_OK);
    if (a != HAL_OK) g_demo_diag.console_uart_errors++;
  } else {
    g_demo_diag.console_rx_irq_armed = 0U;
  }
#else
  (void)a;
  g_demo_diag.console_rx_irq_armed = 0U;
#endif

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
#if !G1_DIAGNOSTICS_UART_ENABLE
  (void)s; /* no blocking USART2 TX or monitor dependency in product image */
  return;
#else
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
#endif
}

/* Robust binary UART sender for capture downloads.  One giant 32-KiB
 * HAL_UART_Transmit() can hit its single overall timeout when the RTOS preempts
 * the ControlTask.  Small chunks give each piece its own timeout and make the
 * Windows VCP transfer deterministic at 115200 baud. */
static HAL_StatusTypeDef uart2_send_binary_chunked(const uint8_t *data, uint32_t len)
{
#if !G1_DIAGNOSTICS_UART_ENABLE
  (void)data; (void)len;
  return HAL_ERROR; /* disabled diagnostics, not a failed product peripheral */
#else
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
#endif
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

/* All three audio SAI blocks share the same PLLSAI1 clock. SAI2A is
 * externally synchronized to the SAI1A master. Thus the NORMAL TC path is
 * synchronous with speaker DMA and must not run an elastic-rate controller:
 * that controller periodically re-primed the output and caused clicks.
 * Keep the elastic renderer compiled as a diagnostic fallback only. */
static bool output_clock_locked_path(void)
{
  /* Microphone DFSDM has a separate conversion clock and still benefits
   * from the elastic FIFO clock-drift fallback. */
  if (g_demo_control.requested_source_mode == DEMO_MODE_MIC_ONLY &&
      g_demo_control.direct_source == DEMO_DIRECT_OFF) return false;
  if (s_tc_use_elastic_output &&
      (g_demo_control.direct_source == DEMO_DIRECT_TC ||
       g_demo_diag.active_source == 1U)) return false;
  return true; /* SAI1A / SAI2A / SAI2B: 48 kHz from one PLLSAI1 */
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
  /* Bench test ceiling, independent of normal gain; no HAL in IRQ. */
  QrDemoTest_Render(&s_test_generator,(uint8_t)g_demo_control.test_tone_step,
                    dst,DEMO_FRAMES_PER_HALF);
}

/* Called directly from the SAI2B DMA callback.  The half that just completed
 * is safe to rewrite immediately, so this removes an entire RTOS scheduling
 * dependency from the physical speaker stream. */
static void speaker_fill_region_isr(uint32_t start_sample)
{
  int16_t *dst = &g_speaker_tx_dma[start_sample];
  bool have = false;

  if (s_audio_sleep_active) {
    memset(dst,0,DEMO_OUTPUT_HALF_SAMPLES*sizeof(int16_t));
    return; /* no cue or audio can escape while soft-asleep */
  }
  if (!g_demo_control.output_enabled) {
    memset(dst, 0, DEMO_OUTPUT_HALF_SAMPLES * sizeof(int16_t));
    /* A short mute acknowledgement is allowed; it is NOT sustained audio. */
    DhaFeedback_Render(dst, DEMO_FRAMES_PER_HALF);
    return;
  }

  if (g_demo_control.test_tone_step != 0U || QrDemoTest_IsActive(&s_test_generator)) {
    speaker_fill_tone_isr(dst);
    /* Calibration audio is standalone; queued user cues must not exceed the
     * bounded signal level or contaminate the 1 kHz/chirp test. */
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
      DhaFeedback_Render(dst, DEMO_FRAMES_PER_HALF);
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
  DhaFeedback_Render(dst, DEMO_FRAMES_PER_HALF);
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
    if (s_tc_filter_reset_requested) {
      s_tc_filter_reset_requested=0U;
      TcCleanup_Reset(&g_r27_tc_filter);
      if(s_tc_notch_auto_enabled && s_tc_hum_last_hz)
        TcCleanup_SetNotchFrequency(&g_r27_tc_filter,s_tc_hum_last_hz);
    }
    TcCleanup_SetNotchAuto(&g_r27_tc_filter,s_tc_notch_auto_enabled!=0U);
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
  /* Context is from actual latest PCM, not an assumed environment.
   * Approximate spectral bucket via 128-sample zero-crossing rate. */
  if (source != 3U) { /* mic supplied its raw pre-AGC features */
    unsigned crossings=0U;
    for (size_t i=1U;i<DEMO_FRAMES_PER_HALF;++i)
      if ((pre[i]>=0)!=(pre[i-1U]>=0))++crossings;
    s_pref_spectrum=(uint8_t)(crossings<8U?G1_PREF_LOW:
                            crossings<32U?G1_PREF_MID:G1_PREF_HIGH);
  }

  if (source != g_last_output_source) {
    output_queue_reset();
    g_plot_accum_count = 0U;
    (void)AudioDynamics_Reset(&g_dynamics);
    g_last_output_source = source;
  }

  metrics_s16(pre, DEMO_FRAMES_PER_HALF, &in_rms, NULL, &in_peak);
  if (source != 3U) { /* preserve raw mic amplitude context */
    s_pref_amp=(uint8_t)(in_rms<350U?G1_PREF_QUIET:
                   in_rms<2500U?G1_PREF_MODERATE:G1_PREF_LOUD);
  }
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

  DhaEffects_Process(g_demo_processed_mono, DEMO_FRAMES_PER_HALF,(uint8_t)source);
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
  if (s == 3U) return "MIC";
  return "NONE";
}

void Demo_SetStage(uint32_t stage) { g_demo_diag.stage = stage; }
void Demo_SetError(uint32_t error_code) { if (g_demo_diag.error_code == 0U) g_demo_diag.error_code = error_code; }

void Demo_ConsoleEarly(const char *message) { console_write(message); }

static void panic_putc(char c)
{
#if !G1_DIAGNOSTICS_UART_ENABLE
  (void)c;return;
#else
  uint32_t guard = 2000000U;
  if ((RCC->APB1ENR1 & RCC_APB1ENR1_USART2EN) == 0U) return;
  if ((USART2->CR1 & USART_CR1_UE) == 0U) return;
  while (((USART2->ISR & USART_ISR_TXE) == 0U) && guard-- > 0U) { }
  if ((USART2->ISR & USART_ISR_TXE) != 0U) USART2->TDR = (uint8_t)c;
#endif
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
    "  Lab TEST ALL/CLOCK/RTOS/DSP/AUDIO/BT unavailable in operating image\r\n"
    "  UI STATUS | UI MAP | BOARD STATUS | SYSTEM CHECK (actual live hardware)\r\n"
    "  SOURCE AUTO|MIC|BT|TC|OFF (or MODE AUTO|MIC|BT|TC|OFF)\r\n"
    "  MUTE ON | MUTE OFF | MUTE TOGGLE | UNMUTE\r\n"
    "  VOL + | VOL - | VOLUME 0..100 (percent of safe digital cap)\r\n"
    "  ADAPT ON | ADAPT OFF | DSP ON | DSP OFF | ML STATUS | ML SCENE\r\n"
    "  BT ON | BT OFF | TC ON | TC OFF | ALL ON | ALL OFF\r\n"
    "  BTLINK AUTO | BTLINK ON | BTLINK OFF\r\n"
    "  AUDIO START | AUDIO STOP | AUDIO STATUS\r\n"
    "  TC STATUS | TC RESTART | TC PROBE | BOOT STATUS (real PCM/DMA/clock observations)\r\n"
    "  TC MODE | TC PCM LOW24(default)|LOW16|UPPER16|MIDDLE16 | TC FILTER ON|OFF | TC NOTCH AUTO|OFF | TC CLEAN\r\n"
    "  TC CHANNEL AUTO|LEFT|RIGHT|MIX | TC BASELINE R264|R34|R23 (decoder A/B)\r\n"
    "  TC 24 STATUS | TC 24 LEVEL ON|OFF | TC 24 CAP 1|2|4 | TC 24 RESTORE\r\n"
    "  TC HUM LEARN (induction TRANSMITTER OFF, TC selected) | TC HUM STATUS | TC HUM ON|OFF|<75..4500>\r\n"
    "  TC FILTER OFF|ON (manual TC bypass of cleanup; does not synthesize data)\r\n"
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
    "  Physical panel (Anna): E8 BT, E9 TC, E10 mute, E11 MIC; bit12 proximity\r\n"
    "  TOUCH STATUS | TOUCH BUS | TOUCH RECOVER | TOUCH RETRY | BT CHANNELS | BT MONO MIX\r\n"
    "  BT PROFILE | BT INIT | BT PAIR | BT QUERY | BT STATUS\r\n"
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
    /* R22: a deliberate TC button/command must not be forced silent by the
     * AUTO-only presence detector.  Preserve physical mute and bounded volume.
     * Controller validity is still measured and used normally in AUTO mode. */
    if (g_demo_control.requested_source_mode == DEMO_MODE_TC_ONLY &&
        g_demo_control.direct_source == DEMO_DIRECT_OFF) {
      g_demo_diag.active_source = 1U;
      g_demo_diag.muted = s_product_ui.muted ? 1U : 0U;
    }
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
  else if (m == DEMO_MODE_OFF || m == DEMO_MODE_MIC_ONLY)
    cmd = CONNECTIVITY_COMMAND_DISABLE_BOTH;
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
  /* ControlTask only publishes settings; never touches AudioTask DSP memory.
   * This avoids a reset/data race during touch-driven source switching. */
  if (s_tc_filter_reset_requested) {
    s_tc_filter_reset_requested=0U;
    TcCleanup_Reset(&g_r27_tc_filter);
    if(s_tc_notch_auto_enabled && s_tc_hum_last_hz)
      TcCleanup_SetNotchFrequency(&g_r27_tc_filter,s_tc_hum_last_hz);
  }
  if (s_tc_hum_apply_hz!=UINT32_MAX) {
    const uint32_t hz=s_tc_hum_apply_hz;
    s_tc_hum_apply_hz=UINT32_MAX;
    TcCleanup_SetNotchFrequency(&g_r27_tc_filter,hz);
    s_tc_notch_auto_enabled=(uint8_t)(hz!=0U);
  }
  TcCleanup_SetNotchAuto(&g_r27_tc_filter,s_tc_notch_auto_enabled!=0U);
  (void)ConnectivityService_ProcessTelecoilBlock(&g_service, mono, g_routed,
                                                  DEMO_FRAMES_PER_HALF, now);
  sync_status_from_service();
  if (g_demo_control.requested_source_mode == DEMO_MODE_TC_ONLY &&
      g_demo_control.direct_source == DEMO_DIRECT_OFF) {
    /* Explicit manual TC selection: play actual SAI2A samples independent
     * of AUTO signal-qualification. No synthetic audio or enforced level
     * adjustment. R26.3 uses R34 LOW24 and bypasses TC cleanup by default. */
    ++s_tc_manual_pcm_blocks;
    g_demo_diag.active_source = 1U;
    g_demo_diag.muted = s_product_ui.muted ? 1U : 0U;
    if (g_r27_tc_clean_enabled) {
      TcCleanup_Block(&g_r27_tc_filter,mono,g_r27_tc_clean,DEMO_FRAMES_PER_HALF);
      process_selected_audio(g_r27_tc_clean, 1U);
    } else process_selected_audio(mono, 1U);
  } else if (g_demo_diag.active_source == 1U) {
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
  /* R22: periodically inspect all four container alignments, never only the
   * selected PCM mode.  An RMS of 1 means near-zero decoded audio, not merely
   * detector threshold rejection. A 2-kHz test should change the bitstream. */
  if ((g_demo_diag.tc_blocks_processed & 31U) == 0U && words != NULL) {
    uint64_t sq[4][2] = {{0U}};
    uint32_t word_or=0U, word_and=UINT32_MAX, nonzero=0U, varying=0U;
    uint32_t hi_nonzero=0U, hi_ones=0U, big_jumps=0U;
    int16_t last_lane[2]={(int16_t)words[0],(int16_t)words[1]};
    uint32_t previous=words[0];
    for (uint32_t i=0U;i<DEMO_HALF_WORDS;++i) {
      const uint32_t w=words[i];
      word_or|=w;word_and&=w;
      nonzero+=(w!=0U)?1U:0U;
      varying+=(w!=previous)?1U:0U;
      hi_nonzero+=((w>>16U)!=0U)?1U:0U;
      hi_ones+=((w>>16U)==0xFFFFU)?1U:0U;
      const uint32_t lane=i&1U;
      const int16_t low=(int16_t)(uint16_t)w;
      if (i>=2U && ((int32_t)low-(int32_t)last_lane[lane]>20000 ||
                       (int32_t)last_lane[lane]-(int32_t)low>20000)) big_jumps++;
      last_lane[lane]=low;
      previous=w;
      for (uint32_t m=0U;m<4U;++m) {
        const int32_t x=PcmTransport_DecodeWordMode(w,(PcmTransportDecodeMode_t)m);
        sq[m][i&1U]+=(uint64_t)((int64_t)x*(int64_t)x);
      }
    }
    s_tc_probe_or=word_or; s_tc_probe_and=word_and;
    s_tc_probe_nonzero=nonzero; s_tc_probe_varying=varying;
    s_tc_probe_hi_nonzero=hi_nonzero; s_tc_probe_hi_ones=hi_ones;
    s_tc_probe_low16_big_jumps=big_jumps;
    for (uint32_t j=0;j<4U;++j) s_tc_probe_first[j]=words[j];
    for (uint32_t m=0;m<4U;++m)
      for(uint32_t ch=0;ch<2U;++ch)
        s_tc_probe_rms[m][ch]=isqrt_u64(sq[m][ch]/DEMO_FRAMES_PER_HALF);
    s_tc_probe_last_ms=now;
    ++s_tc_probe_blocks;
  }
  raw_capture_append(RAW_CAPTURE_SOURCE_TC, words, DEMO_FRAMES_PER_HALF);
  /* R26.4: keep all signed 24 bits through gain calculation.  The old
   * LOW24 >>8 was reducing an observed ~800-code telecoil sample to 3.
   * The DSP sees continuous S16 with headroom; no sample threshold/gate. */
  if (s_tc24_level_reset_requested) {
    s_tc24_level_reset_requested = 0U;
    TcS24Recovery_Init(&s_tc24_level);
  }
  if (s_tc_pcm_decode_mode == PCM_DECODE_LOW24 && s_tc24_level_enabled) {
    TcS24Recovery_SetCap(&s_tc24_level,s_tc24_level_cap_q16);
    if (!TcS24Recovery_Process(&s_tc24_level, words, DEMO_FRAMES_PER_HALF,
                                g_left, g_right)) return;
  } else {
    if (PcmTransport_DecodeStereoWordsMode(words, DEMO_FRAMES_PER_HALF, g_left, g_right,
                             (PcmTransportDecodeMode_t)s_tc_pcm_decode_mode) != PCM_TRANSPORT_OK) return;
  }
  listen_capture_append(RAW_CAPTURE_SOURCE_TC, g_left, g_right, DEMO_FRAMES_PER_HALF);
  metrics_s16(g_left, DEMO_FRAMES_PER_HALF, &lrms, NULL, NULL);
  metrics_s16(g_right, DEMO_FRAMES_PER_HALF, &rrms, NULL, NULL);
  s_tc_last_left_rms=lrms;
  s_tc_last_right_rms=rrms;

  if (g_demo_control.direct_source != DEMO_DIRECT_OFF) {
    if (g_demo_control.direct_source == DEMO_DIRECT_TC) {
      direct_process_block(DEMO_DIRECT_TC, g_left, g_right, lrms, rrms);
      g_demo_diag.tc_rms = (g_demo_control.direct_channel == DEMO_CHANNEL_LEFT) ? lrms :
                           (g_demo_control.direct_channel == DEMO_CHANNEL_RIGHT) ? rrms : ((lrms + rrms) / 2U);
    }
    g_demo_diag.tc_blocks_processed++;
    return;
  }

  /* On a damaged channel, an average can halve signal or cancel inverted
   * samples. Auto preserves the stronger genuine channel where appropriate. */
  uint32_t tc_channel=s_tc_channel_mode;
  if (tc_channel==0U) {
    uint32_t candidate=3U;
    int64_t correlation=0;
    for(size_t i=0U;i<DEMO_FRAMES_PER_HALF;++i)
      correlation+=(int64_t)g_left[i]*(int64_t)g_right[i];
    if(correlation<0)candidate=(lrms>=rrms)?1U:2U;
    else if((uint64_t)lrms>(uint64_t)rrms*2ULL+16ULL)candidate=1U;
    else if((uint64_t)rrms>(uint64_t)lrms*2ULL+16ULL)candidate=2U;
    if(candidate!=s_tc_candidate_channel){s_tc_candidate_channel=candidate;s_tc_candidate_blocks=0U;}
    else if(s_tc_candidate_blocks<96U)s_tc_candidate_blocks++;
    /* Hold 256 ms before any channel change; silence cannot flip routing. */
    if(s_tc_candidate_blocks>=96U && (lrms>32U || rrms>32U))s_tc_auto_channel=candidate;
    tc_channel=s_tc_auto_channel;
  }
  for(size_t i=0U;i<DEMO_FRAMES_PER_HALF;++i){
    g_mono[i]=(tc_channel==1U)?g_left[i]:
              (tc_channel==2U)?g_right[i]:
              Audio_MixStereoToMono(g_left[i],g_right[i]);
  }
  TcHumCal_Feed(&s_tc_hum_cal,g_mono,DEMO_FRAMES_PER_HALF);
  process_tc_pcm(g_mono, now);
}

/* Two independent input receivers, even though SAI1A generates both PCM1808
 * clock sets: bit0=SAI1A master, bit1=SAI2A telecoil. */
static uint32_t dma_desired_topology(void)
{
  if(!g_demo_control.audio_capture_enabled || g_demo_control.test_tone_step!=0U)
    return 0U;
  if(g_demo_control.requested_source_mode==DEMO_MODE_MIC_ONLY &&
     g_demo_control.direct_source==DEMO_DIRECT_OFF) return 0U;
  if(g_demo_control.requested_source_mode==DEMO_MODE_OFF &&
     g_demo_control.direct_source==DEMO_DIRECT_OFF) return 0U;
  if(g_demo_control.requested_source_mode==DEMO_MODE_BT_ONLY ||
     g_demo_control.direct_source==DEMO_DIRECT_BT) return 1U;
  return 3U; /* TC / AUTO / direct TC require the SAI1A master AND SAI2A */
}

static HAL_StatusTypeDef start_dma(void)
{
  HAL_StatusTypeDef a = HAL_OK, b = HAL_OK, c = HAL_OK;
  const uint32_t topology=dma_desired_topology();
  const bool need_bt=(topology&1U)!=0U;
  const bool need_tc=(topology&2U)!=0U;

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
    s_bt_last_start_status=(uint32_t)a;
    if (a == HAL_OK) g_bt_dma_started = 1U;
  }
  if (need_tc) {
    b = HAL_SAI_Receive_DMA(&hsai_BlockA2, (uint8_t *)g_tc_dma_words, DEMO_DMA_WORDS);
    s_tc_last_start_status=(uint32_t)b;
    if (b == HAL_OK) g_tc_dma_started = 1U;
  }

  /* Partial-input operation: a missing PCM1808 must not silence the speaker
   * or an independent input. AudioTask reports the counters for diagnosis. */
  if(a!=HAL_OK && need_bt) ++g_demo_diag.sai1_errors;
  if(b!=HAL_OK && need_tc) ++g_demo_diag.sai2_errors;
  if(c==HAL_OK && ((!need_bt && !need_tc) || g_bt_dma_started || g_tc_dma_started)) {
    g_dma_started = 1U;
    g_dma_topology_mask=topology; /* record requested topology; avoid retry storms if one input fails */
    g_demo_diag.audio_dma_running = (g_bt_dma_started || g_tc_dma_started) ? 1U : 0U;
    return HAL_OK;
  }

  if (g_bt_dma_started) (void)HAL_SAI_DMAStop(&hsai_BlockA1);
  if (g_tc_dma_started) (void)HAL_SAI_DMAStop(&hsai_BlockA2);
  if (g_speaker_dma_started) (void)SpeakerOutput_StopDma();
  g_bt_dma_started = g_tc_dma_started = g_speaker_dma_started = 0U;
  g_dma_topology_mask=0U;
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
  g_dma_topology_mask=0U;
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
        s_bt_last_rx_ms=HAL_GetTick();
        if (g_bt_line_len>=5U && g_bt_line[0]=='T' && g_bt_line[1]=='S' && g_bt_line[2]=='+') {
          if(g_bt_line[3]=='0' && g_bt_line[4]=='0')s_bt_status_code=1U;
          else if(g_bt_line[3]=='0' && g_bt_line[4]=='1')s_bt_status_code=2U;
          else if(g_bt_line[3]=='0' && g_bt_line[4]=='2')s_bt_status_code=3U;
        }
        /* Module responses matter for distinguishing UART failure, unpaired
         * Bluetooth, and working Bluetooth with a silent PCM analogue path. */
        console_write("G1|BT|RX|");
        console_write(g_bt_line);
        console_write("\r\n");
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
  /* Operating image does not create background stress workers. */
  return (g_demo_diag.control_task_loops > 10U && g_demo_diag.audio_task_loops > 10U &&
          g_demo_diag.bt_task_loops > 10U
#if DHA_ENABLE_RTOS_STRESS
          && g_demo_diag.background_task_loops > 10U
#endif
          ) ? 1U : 0U;
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

static void __attribute__((unused)) test_all(void)
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

/* ============================================================================
 * GROUP USER-CONTROL BOUNDARY (ControlTask -> AudioTask)
 * ============================================================================
 * The queued GroupEvent is copied, not a pointer to stack memory. xQueueSend
 * uses zero waiting ticks so a stuck control queue cannot block audio. Queue
 * overflow is observable in group telemetry and is not reported as success.
 *
 * The Nucleo B1 physical button path is not a custom-PCB user-button driver.
 */
static void group_event_send(uint8_t type, uint8_t value)
{
  GroupEvent e = {type, value};
  if (g_group_events == NULL || xQueueSend(g_group_events, &e, 0U) != pdPASS)
    g_group_telemetry.event_drops++;
}
static void group_print_status(void)
{
  (void)snprintf(g_r21_line, sizeof(g_r21_line),
    "GROUP|STATUS|enabled=%lu|auto=%lu|profile=%lu|scene=%lu|gain=%lu|mean=%lu|blocks=%lu|events=%lu|drop=%lu|button=%lu|guard=%lu|guard_events=%lu|recoveries=%lu|ui=%lu|source_changes=%lu|source_mode=%lu\r\n",
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
    (unsigned long)g_group_telemetry.guardian_recoveries,
    (unsigned long)g_group_telemetry.physical_gestures,
    (unsigned long)g_group_telemetry.ui_source_changes,
    (unsigned long)g_demo_control.requested_source_mode);
  console_write(g_r21_line);
}
/* Nucleo-L476RG B1 is PC13, active-low.  NOT valid for arbitrary custom PCB.
 * Poll from low-rate ControlTask; never start a separate scheduler/ISR here. */
#define GROUP_NUCLEO_B1_ENABLED 0
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
  /* LED: off=group DSP off, solid=manual, 100-ms pulse/s=automatic,
   * quick flash=overload. PA5/PC13 are Nucleo pins, NOT custom PCB. */
  GPIO_PinState led=GPIO_PIN_RESET;
  if (g_group_telemetry.guardian_level != 0U)
    led=((now%400U)<100U)?GPIO_PIN_SET:GPIO_PIN_RESET;
  else if (g_group_telemetry.enabled)
    led=(!g_group_telemetry.automatic || (now%1000U)<100U)?GPIO_PIN_SET:GPIO_PIN_RESET;
  /* Avoid redundant HAL writes on each 5-ms control-task poll. */
  static GPIO_PinState last_led=GPIO_PIN_RESET;
  if (led!=last_led) {HAL_GPIO_WritePin(GPIOA,GPIO_PIN_5,led);last_led=led;}
  uint8_t pressed=(HAL_GPIO_ReadPin(GPIOC,GPIO_PIN_13)==GPIO_PIN_RESET)?1U:0U;
  GroupUiGesture gesture=GroupUi_Poll(&g_group_ui,now,pressed);
  if (gesture==GROUP_UI_NONE) return;
  ++g_group_button_seen;
  g_group_telemetry.button_presses=g_group_button_seen;
  if (gesture==GROUP_UI_SINGLE) {
    group_event_send(GROUP_EVENT_ENABLE,1U);
    group_event_send(GROUP_EVENT_PROFILE,(uint8_t)((g_group_telemetry.profile+1U)&3U));
  } else if (gesture==GROUP_UI_DOUBLE) {
    group_event_send(GROUP_EVENT_ENABLE,1U);
    group_event_send(GROUP_EVENT_AUTO,(uint8_t)!g_group_telemetry.automatic);
  } else if (gesture==GROUP_UI_TRIPLE) {
    /* Real routing request, not a synthetic link-state bit.
     * Direct !BS/!TS modes override source FSM until !FA. */
    uint32_t mode=g_demo_control.requested_source_mode;
    uint8_t next=(mode==DEMO_MODE_AUTO)?DEMO_MODE_BT_ONLY:
                 (mode==DEMO_MODE_BT_ONLY)?DEMO_MODE_TC_ONLY:DEMO_MODE_AUTO;
    group_event_send(GROUP_EVENT_SOURCE,next);
  } else if (gesture==GROUP_UI_LONG) {
    group_event_send(GROUP_EVENT_ENABLE,(uint8_t)!g_group_telemetry.enabled);
  } else if (gesture==GROUP_UI_RESET) {
    group_event_send(GROUP_EVENT_SOURCE,DEMO_MODE_AUTO);
    group_event_send(GROUP_EVENT_AUTO,0U);
    group_event_send(GROUP_EVENT_PROFILE,GROUP_PROFILE_NEUTRAL);
    group_event_send(GROUP_EVENT_ENABLE,0U);
  }
#else
  (void)now;
#endif
}
/* Standalone product controls: no USART/PC host needed.
 * Pad functions are a firmware mapping; user-panel artwork/electrode order
 * must match the routed copper electrodes before release. */
/* Small bounded adaptive preference learner: explicit user volume corrections
 * are labels; amplitude and zero-crossing spectrum define the context.
 * No autonomous unbounded gain increases, model is RAM-only. */
static void product_apply_preference(void)
{
  int32_t gain=(int32_t)s_user_gain_q15;
  if (s_pref_enabled) {
    int32_t db_q8=G1Pref_Get(&s_product_preferences,
      (G1PrefAmplitude)s_pref_amp,(G1PrefSpectrum)s_pref_spectrum);
    /* First-order dB-to-gain approximation for only [-6,+3] dB.
     * Never exceeds the bounded UI gain; actual ear SPL is uncalibrated. */
    gain+=(gain*db_q8*3)/(256*25);
  }
  if (gain<0)gain=0;
  if (gain>(int32_t)G1_OUTPUT_MAX_Q15)gain=(int32_t)G1_OUTPUT_MAX_Q15;
  g_demo_control.output_gain_q15=(uint32_t)gain;
}
/* One ControlTask-owned state machine. Every physical electrode, rotary detent
 * and normal UART action uses this same codepath; log actual state changes,
 * never claim a hardware source is present just because it was selected. */
static void r10_snapshot(const char *tag)
{
  /* REAL counters only. No mock/pass flags: changes over time matter.
   * Emitted from ControlTask, not inside high-frequency audio DMA ISRs. */
  (void)snprintf(g_r21_line,sizeof(g_r21_line),
    "G1|%s|ms=%lu|source=%s|active=%lu|mute=%lu|vol=%lu|out=%lu|mic_run=%lu|micblk=%lu|micpeak=%lu|micerr=%lu|touch=%lu|addr=0x%02lX|raw=0x%03lX|tread=%lu|terr=%lu|encirq=%lu|encpoll=%lu|det=%lu|bt_uart_rx=%lu|bt_pcm=%lu|bt_rms=%lu|tc_pcm=%lu|tc_rms=%lu|spk_dma=%lu|spkblk=%lu|under=%lu|err=0x%lX\r\n",
    tag,(unsigned long)HAL_GetTick(),G1Ui_SourceName(s_product_ui.source),
    (unsigned long)g_demo_diag.active_source,(unsigned long)s_product_ui.muted,
    (unsigned long)G1Ui_Percent(&s_product_ui),(unsigned long)g_demo_control.output_enabled,
    (unsigned long)g_g1_board.mic_running,(unsigned long)g_g1_board.mic_blocks,
    (unsigned long)g_g1_board.mic_peak,(unsigned long)g_g1_board.mic_errors,
    (unsigned long)g_g1_board.touch_present,(unsigned long)g_g1_board.touch_address,
    (unsigned long)g_g1_board.touch_raw,(unsigned long)g_g1_board.touch_reads,
    (unsigned long)g_g1_board.touch_errors,(unsigned long)g_g1_board.encoder_irq_edges,
    (unsigned long)g_g1_board.encoder_poll_edges,(unsigned long)g_g1_board.encoder_detents,
    (unsigned long)g_demo_diag.bt_uart_rx_bytes,(unsigned long)g_demo_diag.bt_blocks_processed,
    (unsigned long)g_demo_diag.bt_rms,(unsigned long)g_demo_diag.tc_blocks_processed,
    (unsigned long)g_demo_diag.tc_rms,(unsigned long)g_demo_diag.speaker_dma_running,
    (unsigned long)g_demo_diag.speaker_blocks_output,(unsigned long)g_demo_diag.speaker_underruns,
    (unsigned long)g_demo_diag.error_code);
  console_write(g_r21_line);
}

static void r10_hardware_report(void)
{
  uint32_t a = (HAL_GPIO_ReadPin(GPIOA, GPIO_PIN_0)==GPIO_PIN_SET)?1U:0U;
  uint32_t b = (HAL_GPIO_ReadPin(GPIOA, GPIO_PIN_1)==GPIO_PIN_SET)?1U:0U;
  uint32_t scl = (HAL_GPIO_ReadPin(GPIOB, GPIO_PIN_6)==GPIO_PIN_SET)?1U:0U;
  uint32_t sda = (HAL_GPIO_ReadPin(GPIOB, GPIO_PIN_7)==GPIO_PIN_SET)?1U:0U;
  (void)snprintf(g_r21_line,sizeof(g_r21_line),
    "G1|PINS|encA=%lu|encB=%lu|scl=%lu|sda=%lu|touch_ack=%lu|addr=0x%02lX|mic_ready=%lu|mic_running=%lu|initerr=0x%lX\r\n",
    (unsigned long)a,(unsigned long)b,(unsigned long)scl,(unsigned long)sda,
    (unsigned long)g_g1_board.touch_present,(unsigned long)g_g1_board.touch_address,
    (unsigned long)g_g1_board.mic_ready,(unsigned long)g_g1_board.mic_running,
    (unsigned long)g_g1_board.init_error_bits);
  console_write(g_r21_line);
}

static void product_ui_status(void)
{
  (void)snprintf(g_r21_line,sizeof(g_r21_line),
    "G1|UI|STATUS|source=%s|mode=%lu|mute=%lu|volume_pct=%lu|gain_q15=%lu|dsp=%lu|adaptive=%lu|active=%lu|touch=%lu|micblocks=%lu|btblocks=%lu|tcblocks=%lu|speaker=%lu|initerr=%lu\r\n",
    G1Ui_SourceName(s_product_ui.source),
    (unsigned long)g_demo_control.requested_source_mode,
    (unsigned long)s_product_ui.muted,
    (unsigned long)G1Ui_Percent(&s_product_ui),
    (unsigned long)g_demo_control.output_gain_q15,
    (unsigned long)s_product_ui.dsp,
    (unsigned long)s_product_ui.adaptive,
    (unsigned long)g_demo_diag.active_source,
    (unsigned long)g_g1_board.touch_present,
    (unsigned long)g_g1_board.mic_blocks,
    (unsigned long)g_demo_diag.bt_blocks_processed,
    (unsigned long)g_demo_diag.tc_blocks_processed,
    (unsigned long)g_demo_diag.speaker_blocks_output,
    (unsigned long)g_g1_board.init_error_bits);
  console_write(g_r21_line);
}
static void product_ui_report(const char *via,const char *field,const char *value)
{
  (void)snprintf(g_r21_line,sizeof(g_r21_line),
    "G1|EVENT|via=%s|field=%s|value=%s|source=%s|mute=%lu|volume_pct=%lu|gain_q15=%lu\r\n",
    via,field,value,G1Ui_SourceName(s_product_ui.source),
    (unsigned long)s_product_ui.muted,
    (unsigned long)G1Ui_Percent(&s_product_ui),
    (unsigned long)g_demo_control.output_gain_q15);
  console_write(g_r21_line);
}
static void product_ui_commit(uint32_t changed,const char *via,int user_gain_direction)
{
  if(changed&G1_UI_SOURCE_CHANGED){
    if(g_demo_control.direct_source!=DEMO_DIRECT_OFF){
      /* AudioTask alone is allowed to stop/restart DMA and FSM. */
      g_demo_control.direct_source=DEMO_DIRECT_OFF;
      g_r28_route_restart_pending=1U;
    }
    g_demo_control.requested_source_mode=s_product_ui.source;
    /* A new TC visit learns the interference from the new physical session.
     * Reset is consumed later by AudioTask, which owns the DSP state. */
    s_tc_filter_reset_requested=1U;
    /* Do not attempt undocumented AVRCP absolute-volume control of phones.
     * 100% means local bounded digital gain, not acoustic full-power. */
    /* AudioTask is the only DMA owner; force a fresh capture configuration
     * whenever the user changes source, including MIC->BT/TC and BT->MIC. */
    g_r28_route_restart_pending=1U;
    g_demo_control.audio_capture_enabled=1U;
    g_demo_control.test_tone_step=0U;
    s_test_saved_valid=0U; /* source tap cancels any active bench signal */
  }
  if(changed&G1_UI_GAIN_CHANGED){
    s_user_gain_q15=s_product_ui.gain_q15;
    if(s_pref_enabled && user_gain_direction!=0){
      (void)G1Pref_Feedback(&s_product_preferences,
        (G1PrefAmplitude)s_pref_amp,(G1PrefSpectrum)s_pref_spectrum,
        user_gain_direction>0?G1_PREF_INCREASE:G1_PREF_DECREASE);
    }
  }
  if(changed&G1_UI_DSP_CHANGED)g_demo_control.dsp_enabled=s_product_ui.dsp;
  if(changed&G1_UI_PREF_CHANGED)s_pref_enabled=s_product_ui.adaptive;
  if(changed&(G1_UI_GAIN_CHANGED|G1_UI_PREF_CHANGED))product_apply_preference();
  if(changed&(G1_UI_SOURCE_CHANGED|G1_UI_MUTE_CHANGED)){
    /* Output OFF is separate from zero volume; SOURCE OFF also gates output. */
    g_demo_control.output_enabled=(uint32_t)(!s_product_ui.muted && s_product_ui.source!=G1_UI_OFF);
  }
  if(changed&G1_UI_SOURCE_CHANGED){
    switch(s_product_ui.source){
      case G1_UI_MIC:DhaFeedback_Request(DHA_CUE_MIC);break;
      case G1_UI_BT:DhaFeedback_Request(DHA_CUE_BT);break;
      case G1_UI_TC:DhaFeedback_Request(DHA_CUE_TC);break;
      default:break;
    }
    product_ui_report(via,"source",G1Ui_SourceName(s_product_ui.source));
  }
  if(changed&G1_UI_MUTE_CHANGED){
    DhaFeedback_Request(s_product_ui.muted?DHA_CUE_MUTE:DHA_CUE_UNMUTE);
    product_ui_report(via,"mute",s_product_ui.muted?"ON":"OFF");
  }
  if(changed&G1_UI_SPATIAL_CHANGED){
    DhaEffects_SetRoom(s_product_ui.spatial);
    DhaFeedback_Request(s_product_ui.spatial?DHA_CUE_SPATIAL_ON:DHA_CUE_SPATIAL_OFF);
    product_ui_report(via,"room",s_product_ui.spatial?"ON":"OFF");
  }
  if(changed&G1_UI_NOISE_CHANGED){
    DhaEffects_SetNoiseReduction(s_product_ui.noise_reduce);
    DhaFeedback_Request(s_product_ui.noise_reduce?DHA_CUE_NR_ON:DHA_CUE_NR_OFF);
    product_ui_report(via,"mic_rumble_filter",s_product_ui.noise_reduce?"ON":"OFF");
  }
  if(changed&G1_UI_GAIN_CHANGED){
    char number[12];
    (void)snprintf(number,sizeof(number),"%lu",(unsigned long)G1Ui_Percent(&s_product_ui));
    product_ui_report(via,"volume_pct",number);
  }
  if(changed&G1_UI_DSP_CHANGED)product_ui_report(via,"dsp",s_product_ui.dsp?"ON":"OFF");
  if(changed&G1_UI_PREF_CHANGED)product_ui_report(via,"adaptive",s_product_ui.adaptive?"ON":"OFF");
}
/* Four physical pads E8..E11 only: these are long-press functions,
 * not fictional new electrodes. Never synthesise maximum acoustic power. */
static void qr_test_select(uint8_t requested)
{
  uint8_t previous=(uint8_t)g_demo_control.test_tone_step;
  if(s_qr_sleep.asleep)return;
  if(requested==previous)requested=0U; /* press again to stop */
  if(requested!=0U) DhaFeedback_ClearPending(); /* no stale cues over test stimulus */
  if(requested!=0U && !s_test_saved_valid){
    s_test_saved_muted=s_product_ui.muted;
    s_test_saved_source=s_product_ui.source;
    s_test_saved_valid=1U;
  }
  if(requested!=0U){
    (void)G1Ui_Command(&s_product_ui,"MUTE OFF");
    g_demo_control.output_enabled=1U;
    g_demo_control.audio_capture_enabled=1U; /* speaker-only topology */
    g_demo_control.direct_source=DEMO_DIRECT_OFF;
  } else if(s_test_saved_valid){
    s_product_ui.muted=s_test_saved_muted;
    s_product_ui.source=s_test_saved_source;
    g_demo_control.requested_source_mode=s_product_ui.source;
    g_demo_control.output_enabled=(uint32_t)(!s_product_ui.muted && s_product_ui.source!=G1_UI_OFF);
    s_test_saved_valid=0U;
  }
  g_demo_control.test_tone_step=requested;
  g_demo_diag.test_tone_hz=requested==1U?1000U:requested==4U?250U:requested*1000U;
  g_r28_route_restart_pending=1U; /* AudioTask rebuilds correct DMA topology */
}
static void qr_sleep_enter(void)
{
  if(s_qr_sleep.asleep)return;
  if(g_demo_control.test_tone_step)qr_test_select(0U);
  DhaFeedback_ClearPending();
  QrSleep_Enter(&s_qr_sleep);
  s_audio_sleep_active=1U; /* immediately silence the ISR independent of AudioTask */
  g_demo_control.test_tone_step=0U;
  g_demo_diag.test_tone_hz=0U;
  g_demo_control.output_enabled=0U;
  g_demo_control.audio_capture_enabled=0U;
  g_demo_control.requested_source_mode=DEMO_MODE_OFF;
  g_demo_control.direct_source=DEMO_DIRECT_OFF;
  s_bt_sleep_transition=1U; /* BtTask: disconnect + disable Bluetooth audio/EDR */
}
static void qr_sleep_wake(void)
{
  /* Called only after the MPR121 proximity signal is clear then stable-high. */
  s_audio_sleep_active=0U;
  g_demo_control.requested_source_mode=s_product_ui.source;
  g_demo_control.audio_capture_enabled=1U;
  g_demo_control.output_enabled=(uint32_t)(!s_product_ui.muted && s_product_ui.source!=G1_UI_OFF);
  g_r28_route_restart_pending=1U;
  s_tc_filter_reset_requested=1U;
  s_bt_sleep_transition=2U; /* BtTask: restore Bluetooth mode and discovery */
}

static void product_ui_poll(uint32_t now)
{
  G1PhysicalInput event;
  G1_BoardPoll(now,&event); /* must keep I2C1 alive during logical sleep */
  static uint8_t last_proximity=2U;
  uint8_t proximity=(uint8_t)(g_g1_board.touch_proximity!=0U);
  uint8_t present=(uint8_t)(g_g1_board.touch_present!=0U);
  if(present){
    if(last_proximity!=proximity){
      last_proximity=proximity;
      product_ui_report("MPR121","proximity",proximity?"DETECTED":"CLEAR");
    }
  }else last_proximity=2U;
  static uint8_t last_touch_state=2U;
  if(present!=last_touch_state){
    last_touch_state=present;
    product_ui_report("HARDWARE","touch",present?"READY":"MISSING");
  }
  if(s_qr_sleep.asleep){
    if(QrSleep_Update(&s_qr_sleep,present,proximity,now)){
      qr_sleep_wake();
      product_ui_report("MPR121","sleep","WOKE_ON_PROXIMITY");
    }
    return; /* encoder and every pad intentionally ignored */
  }
  /* Combined E8+E11 long press forces logical sleep; proximity wakes. */
  static uint32_t sleep_chord_since=0U;
  static uint8_t sleep_chord_original_source=G1_UI_MIC;
  uint16_t held=(uint16_t)(g_g1_board.touch_raw&0x0F00U);
  if((held&((1U<<8)|(1U<<11)))==((1U<<8)|(1U<<11))){
    if(!sleep_chord_since){
      sleep_chord_since=now;
      sleep_chord_original_source=s_product_ui.source;
    }
    if((uint32_t)(now-sleep_chord_since)>=1500U){
      /* Undo incidental source changes from the two-pad sleep gesture. */
      s_product_ui.source=sleep_chord_original_source;
      qr_sleep_enter();
      sleep_chord_since=0U;
      product_ui_report("TOUCH_HOLD","sleep","ON");
      return;
    }
  }else sleep_chord_since=0U;
  if((uint32_t)(now-s_last_observed_ms)>=500U){
    s_last_observed_ms=now;
    (void)G1Pref_Observe(&s_product_preferences,
      (G1PrefAmplitude)s_pref_amp,(G1PrefSpectrum)s_pref_spectrum);
    product_apply_preference();
  }
  if(event.encoder_clicks){
    uint32_t changed=G1Ui_Encoder(&s_product_ui,(int)event.encoder_clicks);
    product_ui_commit(changed,"ENCODER",(int)event.encoder_clicks);
  }
  /* E8 hold: spatial; E11 hold: microphone rumble reduction.
   * Chord must not fire either single-key long action. */
  static uint32_t long_since=0U;
  static uint16_t long_key=0U;
  static uint8_t long_fired=0U;
  uint16_t spatial_held=held&((1U<<8)|(1U<<11));
  if(spatial_held!=long_key){long_key=spatial_held;long_since=now;long_fired=0U;}
  if((spatial_held==(1U<<8)||spatial_held==(1U<<11)) && !long_fired &&
      (uint32_t)(now-long_since)>=1000U){
    long_fired=1U;
    product_ui_commit(G1Ui_Command(&s_product_ui,
        spatial_held==(1U<<8)?"SPATIAL TOGGLE":"NOISE TOGGLE"),"TOUCH_HOLD",0);
  }
  /* E9: tap TC, hold 1 s toggles 1 kHz bench tone.
   * E10: tap mute, hold 1 s toggles repeating chirp.
   * Defer E10's short action until release, preventing a mute transient
   * immediately before a chirp request. */
  static uint32_t tc_down=0U,mute_down=0U;
  static uint8_t tc_long=0U,mute_long=0U;
  if((held&(1U<<9))){
    if(!tc_down){tc_down=now;tc_long=0U;}
    if(!tc_long && (uint32_t)(now-tc_down)>=1000U){
      tc_long=1U;qr_test_select(1U);
      product_ui_report("TOUCH_HOLD","test","1KHZ_TOGGLE");
    }
  }else tc_down=0U;
  if(held&(1U<<10)){
    if(!mute_down){mute_down=now;mute_long=0U;}
    if(!mute_long && (uint32_t)(now-mute_down)>=1000U){
      mute_long=1U;qr_test_select(4U);
      product_ui_report("TOUCH_HOLD","test","CHIRP_TOGGLE");
    }
  }else if(mute_down){
    if(!mute_long){
      product_ui_commit(G1Ui_Touch(&s_product_ui,(uint16_t)(1U<<10)),"TOUCH",0);
    }
    mute_down=0U;
  }
  uint16_t e=event.touch_rising & (uint16_t)~(1U<<10);
  if(!e)return;
  (void)snprintf(g_r21_line,sizeof(g_r21_line),
    "G1|INPUT|via=TOUCH|rising=0x%03X\r\n",(unsigned)e);
  console_write(g_r21_line);
  uint32_t changed=G1Ui_Touch(&s_product_ui,e);
  product_ui_commit(changed,"TOUCH",0);
}

/* AUDIO TASK ONLY — group control-plane handoff and optional DSP governor.
 * This function applies queued requests at the audio task boundary. Do not
 * call it from a UART callback or another RTOS task: GroupFeatures and
 * GroupGuardian have deliberately single-writer state, no mutex/heap.
 * The overload fallback bypasses ONLY optional G1 filters; it does not stop
 * the original Bluetooth/telecoil DMA transport or mute physical output.
 */
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
    if (ev.type==GROUP_EVENT_SOURCE && ev.value<=DEMO_MODE_TC_ONLY) {
      g_demo_control.requested_source_mode=ev.value;
      ++g_group_ui_source_events;
    }
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
  g_group_telemetry.ui_source_changes=g_group_ui_source_events;
  g_group_telemetry.physical_gestures=g_group_ui.dispatch_count;
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
  if (line_eq(line, "!GH")) {console_write("G5|HELP|B1=1x PROFILE,2x AUTO,3x SOURCE,hold ON/OFF,hold4s RESET;!GP=STATUS;G1 DSP default OFF\r\n");return true;}
  if (line_eq(line, "!GON")) {group_event_send(GROUP_EVENT_ENABLE,1U); console_write("GROUP|ACK|ON\r\n"); return true;}
  if (line_eq(line, "!GOFF")) {group_event_send(GROUP_EVENT_ENABLE,0U); console_write("GROUP|ACK|OFF\r\n"); return true;}
  if (line_eq(line, "!GA")) {group_event_send(GROUP_EVENT_ENABLE,1U);group_event_send(GROUP_EVENT_AUTO,1U);console_write("GROUP|ACK|AUTO_LEVEL\r\n");return true;}
  if (line_eq(line, "!GM")) {group_event_send(GROUP_EVENT_AUTO,0U);console_write("GROUP|ACK|MANUAL\r\n");return true;}
  if (line_eq(line, "!G0") || line_eq(line, "!G1") || line_eq(line, "!G2") || line_eq(line, "!G3")) {
    group_event_send(GROUP_EVENT_ENABLE,1U);
    group_event_send(GROUP_EVENT_PROFILE,(uint8_t)(line[2]-'0'));
    console_write("GROUP|ACK|PROFILE_QUEUED\r\n");return true;
  }
  if (line_eq(line, "!HI")) { console_write("R21|HELLO|PASS|fw=28|base=R21_AUDIO|ctrl=R28|build=R34_PRIORITY7_CONTINUOUS_SPIN|group=G5_STANDALONE_UI_NOTIFY\r\n"); return true; }
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
  if (line_eq(line, "!TN")) { g_r27_tc_clean_enabled=1U; s_tc_filter_reset_requested=1U; console_write("R27|TC_CLEAN|PASS|enabled=1\r\n"); return true; }
  if (line_eq(line, "!TF")) { g_r27_tc_clean_enabled=0U; s_tc_filter_reset_requested=1U; console_write("R27|TC_CLEAN|PASS|enabled=0\r\n"); return true; }
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
#if !DHA_ENABLE_RTOS_STRESS
  /* Operating firmware must not activate the individual CPU stress demos. */
  if (line_eq(line, "!RA") || line_eq(line, "!RS") ||
      line_eq(line, "!RD") || line_eq(line, "!RH")) {
    console_write("PRODUCT|REJECTED|reason=rtos_stress_disabled_in_operating_build\r\n");
    return true;
  }
#endif
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
  product_ui_commit(G1Ui_Command(&s_product_ui,"SOURCE AUTO"),"SERIAL",0);
  g_demo_control.direct_source = DEMO_DIRECT_OFF;
  g_demo_control.audio_capture_enabled = 1U;
  g_demo_control.output_enabled = (uint32_t)!s_product_ui.muted;
  g_demo_control.plot_enabled = 0U;
  /* Preserve user DSP and safe volume settings across mode changes. */
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
  if (line_eq(line, "R21 HELLO")) { console_write("R21|HELLO|PASS|fw=28|base=R21_AUDIO|ctrl=R28|build=R34_PRIORITY7_CONTINUOUS_SPIN|group=G5_STANDALONE_UI_NOTIFY\r\n"); return true; }
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
  /* The production commands and touch/encoder use exactly one state machine.
   * Legacy diagnostic commands remain below for expert bench use. */
  if(line_eq(line,"UI STATUS") || line_eq(line,"PRODUCT STATUS")){
    product_ui_status();++g_demo_diag.console_commands_ok;return;
  }
  if(line_eq(line,"WATCH ON")) {s_r10_watch_enabled=1U; console_write("G1|ACK|WATCH|ON|period_ms=1000\r\n");return;}
  if(line_eq(line,"WATCH OFF")) {s_r10_watch_enabled=0U; console_write("G1|ACK|WATCH|OFF\r\n");return;}
  if(line_eq(line,"WATCH NOW")||line_eq(line,"DIAG ALL")||line_eq(line,"SYSTEM CHECK")) {
    r10_snapshot("SNAP");r10_hardware_report();
    console_write("G1|DIAG|EXPECT|encirq_or_encpoll_increments|touch_ack_and_tread_increment|micblk_increments_in_MIC_mode|bt_uart_rx_only_if_module_responds|bt_pcm_requires_audio|spkblk_requires_output|no_automatic_PASS\r\n");
    return;
  }
  if(line_eq(line,"PINS")) {r10_hardware_report();return;}
  if(line_eq(line,"BOARD REG") || line_eq(line,"GPIO REG")) {
    (void)snprintf(g_r21_line,sizeof(g_r21_line),
      "G1|REG|pa_moder=0x%08lX|pa_pupdr=0x%08lX|pa_idr=0x%08lX|pb_moder=0x%08lX|pb_pupdr=0x%08lX|pb_idr=0x%08lX|pc_moder=0x%08lX|pc_idr=0x%08lX\r\n",
      (unsigned long)GPIOA->MODER,(unsigned long)GPIOA->PUPDR,
      (unsigned long)GPIOA->IDR,(unsigned long)GPIOB->MODER,
      (unsigned long)GPIOB->PUPDR,(unsigned long)GPIOB->IDR,
      (unsigned long)GPIOC->MODER,(unsigned long)GPIOC->IDR);
    console_write(g_r21_line);return;
  }
  if(line_eq(line,"MIC INIT") || line_eq(line,"MIC RETRY")) {
    s_mic_reinit_request=1U;
    console_write("G1|MIC|INIT|QUEUED|check_MIC_STATUS_after_one_second\r\n");return;
  }
  if(line_eq(line,"SYSTEM CHECK") || line_eq(line,"DIAG PRODUCT")) {
    (void)snprintf(g_r21_line,sizeof(g_r21_line),
      "G1|CHECK|mic_ready=%lu|mic_running=%lu|mic_stage=%lu|mic_blocks=%lu|mic_peak=%lu|touch_ack=%lu|touch_reads=%lu|touch_errors=%lu|enc_edges=%lu|enc_detents=%lu|bt_uart_rx=%lu|bt_audio=%lu|tc_audio=%lu|speaker_blocks=%lu|mute=%lu|source=%lu\r\n",
      (unsigned long)g_g1_board.mic_ready,(unsigned long)g_g1_board.mic_running,
      (unsigned long)g_g1_board.mic_init_stage,(unsigned long)g_g1_board.mic_blocks,
      (unsigned long)g_g1_board.mic_peak,(unsigned long)g_g1_board.touch_present,
      (unsigned long)g_g1_board.touch_reads,(unsigned long)g_g1_board.touch_errors,
      (unsigned long)g_g1_board.encoder_irq_edges,(unsigned long)g_g1_board.encoder_detents,
      (unsigned long)g_demo_diag.bt_uart_rx_bytes,(unsigned long)g_demo_diag.bt_blocks_processed,
      (unsigned long)g_demo_diag.tc_blocks_processed,(unsigned long)g_demo_diag.speaker_blocks_output,
      (unsigned long)s_product_ui.muted,(unsigned long)g_demo_control.requested_source_mode);
    console_write(g_r21_line);return;
  }
  if(line_eq(line,"TOUCH RETRY") || line_eq(line,"TOUCH PROBE")) {
    G1_TouchReprobe();
    console_write("G1|TOUCH|REPROBE_REQUESTED|check_BOARD_RAW_for_real_ACK\r\n");
    return;
  }
  if(line_eq(line,"MIC ENHANCE") || line_eq(line,"ML STATUS")) {
    unsigned index=(unsigned)s_pref_amp*G1_PREF_SPECTRAL_BINS+(unsigned)s_pref_spectrum;
    (void)snprintf(g_r21_line,sizeof(g_r21_line),
      "G1|MIC|ENHANCE|agc_q12=%lu|rms=%lu|noise_rms=%lu|speech_likely=%lu|peak_limits=%lu|quiet_atten=%lu|online_learning=%lu|bucket=%u|observations=%u|feedback=%u|learned_db_q8=%d\r\n",
      (unsigned long)s_mic_agc_q12,(unsigned long)s_mic_signal_rms,
      (unsigned long)s_mic_noise_rms,(unsigned long)s_mic_speech_likely,
      (unsigned long)s_mic_agc_limited,(unsigned long)s_mic_noise_attenuated,
      (unsigned long)s_pref_enabled,index,
      (unsigned)s_product_preferences.observations[index],
      (unsigned)s_product_preferences.feedback_events[index],
      (int)s_product_preferences.gain_db_q8[index]);
    console_write(g_r21_line);return;
  }
  if (line_eq(line,"ML SCENE") || line_eq(line,"AMBIENT STATUS")) {
    (void)snprintf(g_r21_line,sizeof(g_r21_line),
      "G1|AMBIENT|rms=%lu|peak=%lu|gain_q12=%lu|floor=%lu|out_rms=%lu|limit_samples=%lu|cluster=%u|cluster_margin_pct=%u|trained_blocks=%lu|zcr=%u|preference_feedback_only=1\r\n",
      (unsigned long)s_ambient.input_rms,(unsigned long)s_ambient.input_peak,
      (unsigned long)s_ambient.gain_q12,(unsigned long)s_ambient.ambient_floor_rms,
      (unsigned long)s_ambient.output_rms,(unsigned long)s_ambient.limited_samples,
      (unsigned)s_ambient.scene_cluster,
      (unsigned)s_ambient.learner.confidence_pct,
      (unsigned long)s_ambient.learner.trained_blocks,
      (unsigned)s_ambient.zero_crossings);
    console_write(g_r21_line);return;
  }
  if (line_eq(line,"TC CLEAN") || line_eq(line,"TC NOISE")) {
    (void)snprintf(g_r21_line,sizeof(g_r21_line),
      "G1|TC|CLEAN|decode=%lu|filter=%lu|in_rms=%lu|out_rms=%lu|floor=%lu|corr_q15=%lu|gate_q12=%lu|recover_gain_q12=%lu|hold_blocks=%lu|active_blocks=%lu|idle_blocks=%lu|limited=%lu|high_rms=%lu|high_floor=%lu|low_tone_blocks=%lu|zc_hz_est=%lu|notch_hz=%lu|notch_mix_q12=%lu|notch_auto=%lu|notch_locks=%lu\r\n",
      (unsigned long)s_tc_pcm_decode_mode,(unsigned long)g_r27_tc_clean_enabled,
      (unsigned long)g_r27_tc_filter.last_input_rms,
      (unsigned long)g_r27_tc_filter.last_output_rms,
      (unsigned long)g_r27_tc_filter.noise_floor_rms,
      (unsigned long)g_r27_tc_filter.last_corr_q15,
      (unsigned long)(g_r27_tc_filter.gate_gain*4096.0f),
      (unsigned long)(g_r27_tc_filter.signal_gain*4096.0f),
      (unsigned long)g_r27_tc_filter.hold_blocks,
      (unsigned long)g_r27_tc_filter.active_blocks,
      (unsigned long)g_r27_tc_filter.quiet_blocks,
      (unsigned long)g_r27_tc_filter.limiter_samples,
      (unsigned long)g_r27_tc_filter.last_high_rms,
      (unsigned long)g_r27_tc_filter.noise_high_rms,
      (unsigned long)g_r27_tc_filter.low_tone_blocks,
      (unsigned long)g_r27_tc_filter.tone_zc_hz,
      (unsigned long)g_r27_tc_filter.notch_hz,
      (unsigned long)(g_r27_tc_filter.notch_mix*4096.0f),
      (unsigned long)s_tc_notch_auto_enabled,
      (unsigned long)g_r27_tc_filter.notch_locks);
    console_write(g_r21_line);return;
  }
  if(line_eq(line,"MIC STATUS")) {
    (void)snprintf(g_r21_line,sizeof(g_r21_line),
      "G1|MIC|STATUS|ready=%lu|running=%lu|stage=%lu|init_try=%lu|start_try=%lu|hal=%lu|initerr=0x%lX|blocks=%lu|peak=%lu|dma_error=%lu\r\n",
      (unsigned long)g_g1_board.mic_ready,(unsigned long)g_g1_board.mic_running,
      (unsigned long)g_g1_board.mic_init_stage,(unsigned long)g_g1_board.mic_init_attempts,
      (unsigned long)g_g1_board.mic_start_attempts,(unsigned long)g_g1_board.mic_last_hal_status,
      (unsigned long)g_g1_board.init_error_bits,(unsigned long)g_g1_board.mic_blocks,
      (unsigned long)g_g1_board.mic_peak,(unsigned long)g_g1_board.mic_errors);
    console_write(g_r21_line);return;
  }
  if (line_eq(line,"TC MODE") || line_eq(line,"TC CONFIG")) {
    (void)snprintf(g_r21_line,sizeof(g_r21_line),
      "G1|TC|CONFIG|decode=%lu|level24=%lu|cap_q16=%lu|channel=%lu|filter=%lu|rms=%lu|valid=%lu|manual_blocks=%lu\r\n",
      (unsigned long)s_tc_pcm_decode_mode,(unsigned long)s_tc24_level_enabled,
      (unsigned long)s_tc24_level_cap_q16,(unsigned long)s_tc_channel_mode,
      (unsigned long)g_r27_tc_clean_enabled,(unsigned long)g_demo_diag.tc_rms,
      (unsigned long)g_demo_diag.tc_valid,(unsigned long)s_tc_manual_pcm_blocks);
    console_write(g_r21_line);++g_demo_diag.console_commands_ok;return;
  }
  /* R26.3: one-command matched decoder/conditioning trials. The task owning
   * TcCleanup performs the reset; ControlTask only publishes desired states.
   * SAI framing and DMA word widths are unchanged from the working R34 demo. */
  if (line_eq(line,"TC BASELINE R34")) {
    s_tc_pcm_decode_mode=PCM_DECODE_LOW24;
    s_tc24_level_enabled=0U; /* literal R34 >>8 reference for comparison */
    s_tc24_level_reset_requested=1U;
    s_tc_channel_mode=0U;
    g_r27_tc_clean_enabled=0U;
    s_tc_notch_auto_enabled=0U;
    s_tc_filter_reset_requested=1U;
    console_write("G1|TC|BASELINE|R34|decode=LOW24|filter=OFF|notch=OFF|channel=AUTO\r\n");
    ++g_demo_diag.console_commands_ok;return;
  }
  if (line_eq(line,"TC BASELINE R23")) {
    s_tc_pcm_decode_mode=PCM_DECODE_LOW16;
    s_tc24_level_enabled=0U;
    s_tc24_level_reset_requested=1U;
    s_tc_channel_mode=0U;
    g_r27_tc_clean_enabled=0U;
    s_tc_notch_auto_enabled=0U;
    s_tc_filter_reset_requested=1U;
    console_write("G1|TC|BASELINE|R23|decode=LOW16|filter=OFF|notch=OFF|channel=AUTO\r\n");
    ++g_demo_diag.console_commands_ok;return;
  }
  if (line_eq(line,"TC BASELINE R264") || line_eq(line,"TC 24 RESTORE")) {
    s_tc_pcm_decode_mode=PCM_DECODE_LOW24;
    s_tc24_level_enabled=1U;
    s_tc24_level_cap_q16=4U*65536U;
    s_tc24_level_reset_requested=1U;
    s_tc_channel_mode=0U;
    g_r27_tc_clean_enabled=0U;
    s_tc_notch_auto_enabled=0U;
    s_tc_filter_reset_requested=1U;
    console_write("G1|TC|BASELINE|R264|LOW24_RECOVER|cap=4x|gate=OFF|notch=OFF\r\n");return;
  }
  if (line_eq(line,"TC 24 LEVEL ON")) {s_tc_pcm_decode_mode=PCM_DECODE_LOW24;s_tc24_level_enabled=1U;s_tc24_level_reset_requested=1U;console_write("G1|TC|24|LEVEL|ON|full_precision\r\n");return;}
  if (line_eq(line,"TC 24 LEVEL OFF")) {s_tc_pcm_decode_mode=PCM_DECODE_LOW24;s_tc24_level_enabled=0U;s_tc24_level_reset_requested=1U;console_write("G1|TC|24|LEVEL|OFF|R34_shift8\r\n");return;}
  if (line_eq(line,"TC 24 CAP 1")) {s_tc24_level_cap_q16=65536U;console_write("G1|TC|24|CAP|1x\r\n");return;}
  if (line_eq(line,"TC 24 CAP 2")) {s_tc24_level_cap_q16=2U*65536U;console_write("G1|TC|24|CAP|2x\r\n");return;}
  if (line_eq(line,"TC 24 CAP 4")) {s_tc24_level_cap_q16=4U*65536U;console_write("G1|TC|24|CAP|4x\r\n");return;}
  /* R26.6 source-OFF calibration. DO NOT expose automated programme-dependent
   * notching: a genuine 1 kHz signal and a 1 kHz interferer are indistinguishable
   * without a noise-only reference. */
  if (line_eq(line,"TC HUM LEARN") || line_eq(line,"TC CAL START")) {
    if (s_tc_hum_cal.state==TC_HUM_CAPTURE_REQUESTED ||
        s_tc_hum_cal.state==TC_HUM_CAPTURING ||
        s_tc_hum_cal.state==TC_HUM_ANALYSING) {
      console_write("G1|TC|HUM|BUSY|wait_for_result\r\n");return;
    }
    if (g_demo_control.requested_source_mode!=DEMO_MODE_TC_ONLY &&
        g_demo_control.requested_source_mode!=DEMO_MODE_AUTO) {
      console_write("G1|TC|HUM|ERR|select_telecoil_first\r\n");return;
    }
    TcHumCal_Request(&s_tc_hum_cal);
    console_write("G1|TC|HUM|CAPTURING|switch_induction_source_OFF_for_1second\r\n");return;
  }
  if (line_eq(line,"TC HUM STATUS")) {
    (void)snprintf(g_r21_line,sizeof(g_r21_line),
      "G1|TC|HUM|state=%lu|captured=%lu/%u|bins=%lu|hz=%lu|fraction_permille=%lu|notch=%lu|filter=%lu\r\n",
      (unsigned long)s_tc_hum_cal.state,(unsigned long)s_tc_hum_cal.count,
      (unsigned)TC_HUM_CAL_SAMPLES,(unsigned long)s_tc_hum_cal.bins_measured,
      (unsigned long)s_tc_hum_cal.best_hz,(unsigned long)s_tc_hum_cal.peak_fraction_permille,
      (unsigned long)s_tc_hum_last_hz,(unsigned long)g_r27_tc_clean_enabled);
    console_write(g_r21_line);return;
  }
  if (line_eq(line,"TC HUM OFF")) {
    s_tc_hum_apply_hz=0U;s_tc_notch_auto_enabled=0U;
    console_write("G1|TC|HUM|OFF|fixed_notch_bypassed\r\n");return;
  }
  if (line_eq(line,"TC HUM ON")) {
    if(s_tc_hum_last_hz==0U){console_write("G1|TC|HUM|ERR|calibrate_first\r\n");return;}
    s_tc_hum_apply_hz=s_tc_hum_last_hz;
    console_write("G1|TC|HUM|ON|stored_frequency_reapplied\r\n");return;
  }
  if(line[0]=='T' && line[1]=='C' && line[2]==' ' &&
     line[3]=='H' && line[4]=='U' && line[5]=='M' && line[6]==' ' &&
     line[7]>='0' && line[7]<='9'){
    uint32_t hz=0U;unsigned i=7U;
    for(;line[i]>='0' && line[i]<='9' && i<12U;++i){
      hz=hz*10U+(uint32_t)(line[i]-'0');
    }
    if(line[i]!='\0' || hz<75U || hz>4500U){
      console_write("G1|TC|HUM|ERR|use_75_to_4500_Hz\r\n");return;
    }
    s_tc_hum_last_hz=hz;s_tc_hum_apply_hz=hz;
    console_write("G1|TC|HUM|MANUAL|notch_frequency_set\r\n");return;
  }
  if (line_eq(line,"TC 24 STATUS")) {
    (void)snprintf(g_r21_line,sizeof(g_r21_line),"G1|TC|24|enabled=%lu|decoder=%lu|raw_lrms=%lu|raw_rrms=%lu|raw_peak=%lu|gain_q16=%lu|cap_q16=%lu|out_rms=%lu|clips=%lu|blocks=%lu\r\n",
        (unsigned long)s_tc24_level_enabled,(unsigned long)s_tc_pcm_decode_mode,
        (unsigned long)s_tc24_level.left_rms24,(unsigned long)s_tc24_level.right_rms24,
        (unsigned long)s_tc24_level.max_peak24,(unsigned long)s_tc24_level.gain_q16,
        (unsigned long)s_tc24_level.cap_q16,(unsigned long)s_tc24_level.out_rms16,
        (unsigned long)s_tc24_level.clipped_samples,(unsigned long)s_tc24_level.processed_blocks);
    console_write(g_r21_line);return;
  }
  if (line_eq(line,"TC PCM LOW24")) {s_tc_pcm_decode_mode=PCM_DECODE_LOW24;s_tc24_level_enabled=1U;s_tc24_level_reset_requested=1U;s_tc_filter_reset_requested=1U;console_write("G1|TC|PCM|LOW24_RECOVER\r\n");return;}
  if (line_eq(line,"TC PCM UPPER16")) {s_tc_pcm_decode_mode=PCM_DECODE_UPPER16;s_tc_filter_reset_requested=1U;console_write("G1|TC|PCM|UPPER16\r\n");return;}
  if (line_eq(line,"TC PCM MIDDLE16")) {s_tc_pcm_decode_mode=PCM_DECODE_MIDDLE16;s_tc_filter_reset_requested=1U;console_write("G1|TC|PCM|MIDDLE16\r\n");return;}
  if (line_eq(line,"TC PCM LOW16")) {s_tc_pcm_decode_mode=PCM_DECODE_LOW16;s_tc24_level_enabled=0U;s_tc24_level_reset_requested=1U;s_tc_filter_reset_requested=1U;console_write("G1|TC|PCM|LOW16\r\n");return;}
  if (line_eq(line,"TC FILTER OFF")) {g_r27_tc_clean_enabled=0U;s_tc_filter_reset_requested=1U;console_write("G1|TC|FILTER|OFF|raw_pcm_listen\r\n");return;}
  if (line_eq(line,"TC FILTER ON")) {g_r27_tc_clean_enabled=1U;s_tc_filter_reset_requested=1U;console_write("G1|TC|FILTER|ON|continuous_no_gate\r\n");return;}
  if (line_eq(line,"TC NOTCH OFF")) {s_tc_notch_auto_enabled=0U;s_tc_hum_apply_hz=0U;console_write("G1|TC|NOTCH|OFF|speech_filter_only\r\n");return;}
  if (line_eq(line,"TC NOTCH AUTO")) {console_write("G1|TC|NOTCH|DISABLED|use_TC_HUM_LEARN_with_source_off\r\n");return;}
  if (line_eq(line,"TC CHANNEL AUTO")) {s_tc_channel_mode=0U;console_write("G1|TC|CHANNEL|AUTO\r\n");return;}
  if (line_eq(line,"TC CHANNEL LEFT")) {s_tc_channel_mode=1U;console_write("G1|TC|CHANNEL|LEFT\r\n");return;}
  if (line_eq(line,"TC CHANNEL RIGHT")) {s_tc_channel_mode=2U;console_write("G1|TC|CHANNEL|RIGHT\r\n");return;}
  if (line_eq(line,"TC CHANNEL MIX")) {s_tc_channel_mode=3U;console_write("G1|TC|CHANNEL|MIX\r\n");return;}
  if (line_eq(line,"TC PROBE") || line_eq(line,"TC RAW") || line_eq(line,"TC DIAG")) {
    const uint32_t cb=g_demo_diag.tc_half_callbacks+g_demo_diag.tc_full_callbacks;
    const uint32_t mastercb=g_demo_diag.bt_half_callbacks+g_demo_diag.bt_full_callbacks;
    const uint32_t cndtr=(hsai_BlockA2.hdmarx && hsai_BlockA2.hdmarx->Instance)?
        hsai_BlockA2.hdmarx->Instance->CNDTR:0U;
    const uint32_t dmaccr=(hsai_BlockA2.hdmarx && hsai_BlockA2.hdmarx->Instance)?
        hsai_BlockA2.hdmarx->Instance->CCR:0U;
    (void)snprintf(g_r21_line,sizeof(g_r21_line),
      "G1|TC|BUS|cb=%lu|master_cb=%lu|raw_updates=%lu|last_ms=%lu|dma_left=%lu|dma_ccr=0x%08lX|sai2_cr1=0x%08lX|sai2_sr=0x%08lX|sai1_cr1=0x%08lX|sai1_sync=0x%08lX|sai2_sync=0x%08lX|halerr=0x%08lX|dmaerr=0x%08lX\r\n",
      (unsigned long)cb,(unsigned long)mastercb,(unsigned long)s_tc_probe_blocks,
      (unsigned long)s_tc_probe_last_ms,(unsigned long)cndtr,(unsigned long)dmaccr,
      (unsigned long)SAI2_Block_A->CR1,(unsigned long)SAI2_Block_A->SR,
      (unsigned long)SAI1_Block_A->CR1,(unsigned long)SAI1->GCR,(unsigned long)SAI2->GCR,
      (unsigned long)HAL_SAI_GetError(&hsai_BlockA2),
      (unsigned long)(hsai_BlockA2.hdmarx ? hsai_BlockA2.hdmarx->ErrorCode:0U));
    console_write(g_r21_line);
    (void)snprintf(g_r21_line,sizeof(g_r21_line),
      "G1|TC|WORDS|nonzero=%lu/%u|varying=%lu/%u|or=0x%08lX|and=0x%08lX|w0=0x%08lX|w1=0x%08lX|w2=0x%08lX|w3=0x%08lX|actual_rms=%lu|manual_blocks=%lu\r\n",
      (unsigned long)s_tc_probe_nonzero,(unsigned)DEMO_HALF_WORDS,
      (unsigned long)s_tc_probe_varying,(unsigned)DEMO_HALF_WORDS,
      (unsigned long)s_tc_probe_or,(unsigned long)s_tc_probe_and,
      (unsigned long)s_tc_probe_first[0],(unsigned long)s_tc_probe_first[1],
      (unsigned long)s_tc_probe_first[2],(unsigned long)s_tc_probe_first[3],
      (unsigned long)g_demo_diag.tc_rms,(unsigned long)s_tc_manual_pcm_blocks);
    console_write(g_r21_line);
    (void)snprintf(g_r21_line,sizeof(g_r21_line),
      "G1|TC|ALIGN|LOW24_L=%lu|LOW24_R=%lu|UPPER16_L=%lu|UPPER16_R=%lu|MIDDLE16_L=%lu|MIDDLE16_R=%lu|LOW16_L=%lu|LOW16_R=%lu|selected=%lu|channel=%lu|filter=%lu|valid=%lu|muted=%lu|high16_nonzero=%lu|high16_ffff=%lu|low16_jumps=%lu\r\n",
      (unsigned long)s_tc_probe_rms[0][0],(unsigned long)s_tc_probe_rms[0][1],
      (unsigned long)s_tc_probe_rms[1][0],(unsigned long)s_tc_probe_rms[1][1],
      (unsigned long)s_tc_probe_rms[2][0],(unsigned long)s_tc_probe_rms[2][1],
      (unsigned long)s_tc_probe_rms[3][0],(unsigned long)s_tc_probe_rms[3][1],
      (unsigned long)s_tc_pcm_decode_mode,(unsigned long)s_tc_channel_mode,
      (unsigned long)g_r27_tc_clean_enabled,(unsigned long)g_demo_diag.tc_valid,(unsigned long)s_product_ui.muted,
      (unsigned long)s_tc_probe_hi_nonzero,(unsigned long)s_tc_probe_hi_ones,
      (unsigned long)s_tc_probe_low16_big_jumps);
    console_write(g_r21_line);
    ++g_demo_diag.console_commands_ok; return;
  }
  if(line_eq(line,"TC RESTART")) {
    s_tc_restart_requested=1U;
    console_write("G1|TC|RESTART|QUEUED|master_SAI1A_then_slave_SAI2A|inspect_TC_STATUS\r\n");
    ++g_demo_diag.console_commands_ok;return;
  }
  if(line_eq(line,"TC STATUS") || line_eq(line,"TC CHECK")) {
    uint32_t mastercb=g_demo_diag.bt_half_callbacks+g_demo_diag.bt_full_callbacks;
    uint32_t slavecb=g_demo_diag.tc_half_callbacks+g_demo_diag.tc_full_callbacks;
    (void)snprintf(g_r21_line,sizeof(g_r21_line),
      "G1|TC|STATUS|requested=%lu|topology=%lu|master_run=%lu|tc_run=%lu|master_cb=%lu|tc_cb=%lu|tc_blocks=%lu|left_rms=%lu|right_rms=%lu|signal_rms=%lu|pp=%lu|valid=%lu|sai1_err=%lu|sai2_err=%lu|start_bt=%lu|start_tc=%lu|sai2_hal=0x%08lX|dma_err=0x%08lX|speaker=%lu|out_blocks=%lu|decode=%lu|filter=%lu\r\n",
      (unsigned long)g_demo_control.requested_source_mode,
      (unsigned long)g_dma_topology_mask,
      (unsigned long)g_bt_dma_started,(unsigned long)g_tc_dma_started,
      (unsigned long)mastercb,(unsigned long)slavecb,
      (unsigned long)g_demo_diag.tc_blocks_processed,
      (unsigned long)s_tc_last_left_rms,(unsigned long)s_tc_last_right_rms,
      (unsigned long)g_demo_diag.tc_rms,(unsigned long)g_demo_diag.tc_peak_to_peak,
      (unsigned long)g_demo_diag.tc_valid,
      (unsigned long)g_demo_diag.sai1_errors,(unsigned long)g_demo_diag.sai2_errors,
      (unsigned long)s_bt_last_start_status,(unsigned long)s_tc_last_start_status,
      (unsigned long)HAL_SAI_GetError(&hsai_BlockA2),
      (unsigned long)((hsai_BlockA2.hdmarx != NULL) ? hsai_BlockA2.hdmarx->ErrorCode : 0UL),
      (unsigned long)g_demo_diag.speaker_dma_running,
      (unsigned long)g_demo_diag.speaker_blocks_output,
      (unsigned long)s_tc_pcm_decode_mode,
      (unsigned long)g_r27_tc_clean_enabled);
    console_write(g_r21_line);++g_demo_diag.console_commands_ok;return;
  }
  if(line_eq(line,"BOOT STATUS") || line_eq(line,"POWER STATUS")) {
    G1_BootReport();++g_demo_diag.console_commands_ok;return;
  }
  if(line_eq(line,"BT RAW STATUS")) {r10_snapshot("BT");return;}
  if((line[0]=='B'||line[0]=='b') && (line[1]=='T'||line[1]=='t') && line[2]==' ' &&
     (line[3]=='A'||line[3]=='a') && (line[4]=='T'||line[4]=='t') && line[5]==' '){
    const char *cmd=line+6; size_t n=strlen(cmd);
    if(n<3U || n>76U || (upper_ascii(cmd[0])!='A') ||
       (upper_ascii(cmd[1])!='T') || s_r10_bt_at_pending) {
       console_write("G1|ACK|BT_AT|ERR|require_AT_command_and_idle_queue\r\n");return;
    }
    memcpy(s_r10_bt_at_line,cmd,n);
    s_r10_bt_at_line[n++]='\r';s_r10_bt_at_line[n++]='\n';s_r10_bt_at_line[n]='\0';
    __DMB();s_r10_bt_at_pending=1U;
    console_write("G1|ACK|BT_AT|QUEUED|see_BT_RX_for_actual_reply\r\n");return;
  }
  if(line_eq(line,"BOARD RAW") || line_eq(line,"INPUT STATUS")){
    (void)snprintf(g_r21_line,sizeof(g_r21_line),
      "G1|RAW|encoder=%lu|irq=%lu|bad=%lu|detents=%lu|touch=0x%03lX|addr=0x%02lX|present=%lu|reads=%lu|errors=%lu|initerr=%lu\r\n",
      (unsigned long)g_g1_board.encoder_phase,
      (unsigned long)g_g1_board.encoder_irq_edges,
      (unsigned long)g_g1_board.encoder_bad_edges,
      (unsigned long)g_g1_board.encoder_detents,
      (unsigned long)g_g1_board.touch_raw,
      (unsigned long)g_g1_board.touch_address,
      (unsigned long)g_g1_board.touch_present,
      (unsigned long)g_g1_board.touch_reads,
      (unsigned long)g_g1_board.touch_errors,
      (unsigned long)g_g1_board.init_error_bits);
    console_write(g_r21_line);++g_demo_diag.console_commands_ok;return;
  }
  if(line_eq(line,"AUDIO PATH") || line_eq(line,"PATH STATUS")){
    (void)snprintf(g_r21_line,sizeof(g_r21_line),
      "G1|PATH|mode=%lu|active=%lu|mute=%lu|gain=%lu|out=%lu|mic_run=%lu|micblocks=%lu|micpeak=%lu|micerr=%lu|bt=%lu|tc=%lu|speaker_dma=%lu|speaker_blocks=%lu|spk_under=%lu|sai1err=%lu|sai2err=%lu|err=0x%08lX\r\n",
      (unsigned long)g_demo_control.requested_source_mode,
      (unsigned long)g_demo_diag.active_source,(unsigned long)s_product_ui.muted,
      (unsigned long)g_demo_control.output_gain_q15,(unsigned long)g_demo_control.output_enabled,
      (unsigned long)g_g1_board.mic_running,(unsigned long)g_g1_board.mic_blocks,
      (unsigned long)g_g1_board.mic_peak,(unsigned long)g_g1_board.mic_errors,
      (unsigned long)g_demo_diag.bt_blocks_processed,(unsigned long)g_demo_diag.tc_blocks_processed,
      (unsigned long)g_demo_diag.speaker_dma_running,(unsigned long)g_demo_diag.speaker_blocks_output,
      (unsigned long)g_demo_diag.speaker_underruns,
      (unsigned long)g_demo_diag.sai1_errors,(unsigned long)g_demo_diag.sai2_errors,
      (unsigned long)g_demo_diag.error_code);
    console_write(g_r21_line);++g_demo_diag.console_commands_ok;return;
  }
  if(line_eq(line,"BT INIT") || line_eq(line,"BT CONFIGURE")) {
    g_demo_control.apply_bt_profile_request=1U;
    s_bt_pair_request=1U;
    console_write("G1|BT|INIT|QUEUED|playback_EDR_audio_then_discoverable|check_RX\r\n");
    return;
  }
  if(line_eq(line,"BT PAIR") || line_eq(line,"BT DISCOVERABLE")){
    s_bt_pair_request=1U;
    console_write("G1|BT|PAIR|REQUESTED|command=AT+BA06|check_BT_RX_for_ack\r\n");
    ++g_demo_diag.console_commands_ok;return;
  }
  if(line_eq(line,"BT RECOVER")) {
    s_bt_recover_request=1U;
    console_write("G1|BT|RECOVER|QUEUED|UART_RX_rearm_and_query\r\n");return;
  }
  if(line_eq(line,"BT CHANNELS") || line_eq(line,"BT STEREO")) {
    (void)snprintf(g_r21_line,sizeof(g_r21_line),
      "G1|BT|CHANNELS|L_RMS=%lu|R_RMS=%lu|MONO_RMS=%lu|mix=%lu|decode=%lu|blocks=%lu|sai_err=%lu\r\n",
      (unsigned long)g_demo_diag.bt_left_rms,
      (unsigned long)g_demo_diag.bt_right_rms,
      (unsigned long)g_demo_diag.bt_rms,
      (unsigned long)g_demo_control.bt_mono_mode,
      (unsigned long)g_demo_control.pcm_decode_mode,
      (unsigned long)g_demo_diag.bt_blocks_processed,
      (unsigned long)g_demo_diag.sai1_errors);
    console_write(g_r21_line);return;
  }
  if(line_eq(line,"TOUCH STATUS") || line_eq(line,"TOUCH DIAG")) {
    (void)snprintf(g_r21_line,sizeof(g_r21_line),
      "G1|TOUCH|addr=0x%02lX|present=%lu|stage=%lu|ecr=0x%02lX|raw=0x%03lX|prox=%lu|reads=%lu|probes=%lu|acks=%lu|errors=%lu|halerr=0x%lX|halstate=%lu|isr=0x%lX|scl=%lu|sda=%lu\r\n",
      (unsigned long)g_g1_board.touch_address,
      (unsigned long)g_g1_board.touch_present,
      (unsigned long)g_g1_board.touch_stage,
      (unsigned long)g_g1_board.touch_ecr,
      (unsigned long)g_g1_board.touch_raw,
      (unsigned long)g_g1_board.touch_proximity,
      (unsigned long)g_g1_board.touch_reads,
      (unsigned long)g_g1_board.touch_probe_attempts,
      (unsigned long)g_g1_board.touch_acks,
      (unsigned long)g_g1_board.touch_errors,
      (unsigned long)g_g1_board.touch_hal_error,
      (unsigned long)g_g1_board.touch_hal_state,
      (unsigned long)g_g1_board.touch_isr,
      (unsigned long)(HAL_GPIO_ReadPin(GPIOB,GPIO_PIN_6)==GPIO_PIN_SET),
      (unsigned long)(HAL_GPIO_ReadPin(GPIOB,GPIO_PIN_7)==GPIO_PIN_SET));
    console_write(g_r21_line);return;
  }
  if(line_eq(line,"TOUCH BUS") || line_eq(line,"TOUCH BUS STATUS")) {
    (void)snprintf(g_r21_line,sizeof(g_r21_line),
      "G1|TOUCH|BUS|hal_probe=%lu|transport=%s|soft_scan=0x%lX|scans=%lu|scl_rises=%lu|scl_fail=%lu|gpio_mode_b6=%lu|gpio_mode_b7=%lu|af_b6=%lu|af_b7=%lu|scl=%lu|sda=%lu\r\n",
      (unsigned long)g_g1_board.touch_last_probe_status,
      g_g1_board.touch_transport?"GPIO":"HAL",
      (unsigned long)g_g1_board.touch_soft_scan_mask,
      (unsigned long)g_g1_board.touch_soft_scans,
      (unsigned long)g_g1_board.touch_soft_clock_rises,
      (unsigned long)g_g1_board.touch_soft_clock_failures,
      (unsigned long)((GPIOB->MODER>>(6U*2U))&3U),
      (unsigned long)((GPIOB->MODER>>(7U*2U))&3U),
      (unsigned long)((GPIOB->AFR[0]>>(6U*4U))&15U),
      (unsigned long)((GPIOB->AFR[0]>>(7U*4U))&15U),
      (unsigned long)(HAL_GPIO_ReadPin(GPIOB,GPIO_PIN_6)==GPIO_PIN_SET),
      (unsigned long)(HAL_GPIO_ReadPin(GPIOB,GPIO_PIN_7)==GPIO_PIN_SET));
    console_write(g_r21_line);return;
  }
  if(line_eq(line,"TOUCH RECOVER")) {
    G1_TouchReprobe();
    console_write("G1|TOUCH|RECOVER|DEFERRED|I2C1_reset_and_0x5A_reprobe\r\n");return;
  }
  if(line_eq(line,"BT STATUS")){
    (void)snprintf(g_r21_line,sizeof(g_r21_line),
      "G1|BT|uart_rx=%lu|uart_tx=%lu|lines=%lu|profile_attempts=%lu|profile_success=%lu|audio_blocks=%lu|rms=%lu|pcm_present=%lu|irq_armed=%lu|uart_errors=%lu|status=%lu|last_rx_ms=%lu|pair_tx_ms=%lu\r\n",
      (unsigned long)g_demo_diag.bt_uart_rx_bytes,
      (unsigned long)g_demo_diag.bt_uart_tx_bytes,
      (unsigned long)g_demo_diag.bt_uart_lines,
      (unsigned long)g_demo_diag.bt_profile_attempts,
      (unsigned long)g_demo_diag.bt_profile_successes,
      (unsigned long)g_demo_diag.bt_blocks_processed,
      (unsigned long)g_demo_diag.bt_rms,
      (unsigned long)g_demo_diag.bt_audio_present,
      (unsigned long)g_demo_diag.bt_rx_irq_armed,(unsigned long)g_demo_diag.bt_uart_errors,
      (unsigned long)s_bt_status_code,(unsigned long)s_bt_last_rx_ms,
      (unsigned long)s_bt_pair_tx_ms);
    console_write(g_r21_line);++g_demo_diag.console_commands_ok;return;
  }
  if(line_eq(line,"UI MAP")){
    console_write("G1|UI|ANNA_MAP|E8=BLUETOOTH|E9=TELECOIL|E10=MUTE_TOGGLE|E11=MICROPHONE|E0-E7=INACTIVE|BIT12=PROXIMITY|ENCODER=VOLUME\r\n");
    ++g_demo_diag.console_commands_ok;return;
  }
  {
    uint32_t old_gain=s_product_ui.gain_q15;
    uint32_t result=G1Ui_Command(&s_product_ui,line);
    if(result!=G1_UI_UNKNOWN){
      if(result==G1_UI_INVALID){
        console_write("G1|ACK|ERR|reason=BAD_COMMAND_OR_RANGE|hint=HELP\r\n");
        ++g_demo_diag.console_commands_error;
      }else{
        int direction=(s_product_ui.gain_q15>old_gain)?1:
                      (s_product_ui.gain_q15<old_gain)?-1:0;
        product_ui_commit(result,"SERIAL",direction);
        console_write(result?"G1|ACK|OK|changed=1\r\n":"G1|ACK|OK|changed=0\r\n");
        ++g_demo_diag.console_commands_ok;
      }
      return;
    }
  }
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
  if (line_eq(line,"TC OUTPUT EXACT")){s_tc_use_elastic_output=0U;g_r28_route_restart_pending=1U;console_write("TC output: exact-rate synchronous\r\n");return;}
  if (line_eq(line,"TC OUTPUT ELASTIC")){s_tc_use_elastic_output=1U;g_r28_route_restart_pending=1U;console_write("TC output: elastic fallback for bench comparison\r\n");return;}
  if (line_eq(line,"SLEEP ON")){qr_sleep_enter();console_write("OK logical sleep requested\r\n");return;}
  if (line_eq(line,"SLEEP OFF")){s_qr_sleep.asleep=0U;qr_sleep_wake();console_write("OK bench wake requested\r\n");return;}
  if (line_eq(line,"DEMO 1K")){qr_test_select(1U);console_write("OK 1kHz bench output toggle; digital peak capped at 32200\r\n");return;}
  if (line_eq(line,"DEMO CHIRP")){qr_test_select(4U);console_write("OK chirp bench output toggle; digital peak capped at 32200\r\n");return;}
  if (line_eq(line,"DEMO STOP")){if(g_demo_control.test_tone_step)qr_test_select(0U);console_write("OK test output off\r\n");return;}
  if (line_eq(line, "TONE 1K")) { output_queue_reset(); g_demo_control.test_tone_step = 1U; g_demo_diag.test_tone_hz = 1000U; g_tone_phase = 0U; console_write("OK direct output tone 1 kHz\r\n"); return; }
  if (line_eq(line, "TONE 2K")) { output_queue_reset(); g_demo_control.test_tone_step = 2U; g_demo_diag.test_tone_hz = 2000U; g_tone_phase = 0U; console_write("OK direct output tone 2 kHz\r\n"); return; }
  if (line_eq(line, "TONE 3K")) { output_queue_reset(); g_demo_control.test_tone_step = 3U; g_demo_diag.test_tone_hz = 3000U; g_tone_phase = 0U; console_write("OK direct output tone 3 kHz\r\n"); return; }
  if (line_eq(line, "TONE OFF")) { g_demo_control.test_tone_step = 0U; g_demo_diag.test_tone_hz = 0U; g_tone_phase = 0U; output_queue_reset(); console_write("OK direct output tone off\r\n"); return; }
  if (line_eq(line, "CAPTURE BT")) { g_demo_control.audio_capture_enabled = 1U; raw_capture_arm(RAW_CAPTURE_SOURCE_BT); console_write("OK raw BT capture armed: 1024 stereo frames before DSP\r\n"); return; }
  if (line_eq(line, "CAPTURE TC")) { g_demo_control.audio_capture_enabled = 1U; raw_capture_arm(RAW_CAPTURE_SOURCE_TC); console_write("OK raw TC capture armed: 1024 stereo frames before DSP\r\n"); return; }
  if (line_eq(line, "CAPTURE CANCEL")) { raw_capture_arm(RAW_CAPTURE_SOURCE_NONE); console_write("OK raw capture cancelled\r\n"); return; }
  if (line_eq(line, "CAPTURE STATUS")) { console_write("CAPTURE source="); console_write(raw_capture_source_name(g_raw_capture_source)); console_write(" frames="); console_u32(g_raw_capture_frames); console_write(" ready="); console_u32(g_raw_capture_ready); console_write(" decode="); console_u32(g_demo_control.pcm_decode_mode); console_write("\r\n"); return; }
  if (line_eq(line, "CAPTURE DUMP")) { raw_capture_send(); return; }
  if (line_eq(line, "RECORD BT LEFT")) { g_demo_control.audio_capture_enabled = 1U; listen_capture_arm(RAW_CAPTURE_SOURCE_BT, DEMO_CHANNEL_LEFT); console_write("OK pre-DSP BT LEFT record armed: 4096 samples / 85 ms\r\n"); return; }
  if (line_eq(line, "RECORD BT RIGHT")) { g_demo_control.audio_capture_enabled = 1U; listen_capture_arm(RAW_CAPTURE_SOURCE_BT, DEMO_CHANNEL_RIGHT); console_write("OK pre-DSP BT RIGHT record armed: 4096 samples / 85 ms\r\n"); return; }
  if (line_eq(line, "RECORD BT MIX") || line_eq(line, "RECORD BT")) { g_demo_control.audio_capture_enabled = 1U; listen_capture_arm(RAW_CAPTURE_SOURCE_BT, DEMO_CHANNEL_MIX); console_write("OK pre-DSP BT MIX record armed: 4096 samples / 85 ms\r\n"); return; }
  if (line_eq(line, "RECORD TC LEFT")) { g_demo_control.audio_capture_enabled = 1U; listen_capture_arm(RAW_CAPTURE_SOURCE_TC, DEMO_CHANNEL_LEFT); console_write("OK pre-DSP TC LEFT record armed: 4096 samples / 85 ms\r\n"); return; }
  if (line_eq(line, "RECORD TC RIGHT")) { g_demo_control.audio_capture_enabled = 1U; listen_capture_arm(RAW_CAPTURE_SOURCE_TC, DEMO_CHANNEL_RIGHT); console_write("OK pre-DSP TC RIGHT record armed: 4096 samples / 85 ms\r\n"); return; }
  if (line_eq(line, "RECORD TC MIX") || line_eq(line, "RECORD TC")) { g_demo_control.audio_capture_enabled = 1U; listen_capture_arm(RAW_CAPTURE_SOURCE_TC, DEMO_CHANNEL_MIX); console_write("OK pre-DSP TC MIX record armed: 4096 samples / 85 ms\r\n"); return; }
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
  if (line_eq(line, "FX STATUS")) {
    console_write("G1|FX|room=");console_u32(s_product_ui.spatial);
    console_write("|mic_rumble=");console_u32(s_product_ui.noise_reduce);
    console_write("|cue_pending=");console_u32(DhaFeedback_Pending());
    console_write("|anc=UNSUPPORTED_MONO_HARDWARE\r\n");return;
  }
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
#if DHA_ENABLE_RTOS_STRESS
  /* Lab diagnostic command group; not callable in operating image. */
  if (line_eq(line, "TEST CLOCK")) { print_test_result("CLOCK", test_clock()); return; }
  if (line_eq(line, "TEST RTOS")) { print_test_result("RTOS", test_rtos()); return; }
  if (line_eq(line, "TEST DSP")) { print_test_result("DSP", test_dsp()); return; }
  if (line_eq(line, "TEST AUDIO")) { print_test_result("AUDIO", test_audio()); return; }
  if (line_eq(line, "TEST OUTPUT")) { print_test_result("OUTPUT", test_output()); return; }
  if (line_eq(line, "TEST BT")) { print_test_result("BT401", test_bt()); return; }
  if (line_eq(line, "TEST ALL")) { test_all(); return; }
#endif
  if (line_eq(line,"BOARD STATUS")) {
    (void)snprintf(g_r21_line,sizeof(g_r21_line),
     "G1|BOARD|mic=%lu|micblocks=%lu|micpeak=%lu|micov=%lu|micerr=%lu|touch=%lu|touchreads=%lu|toucherr=%lu|encoder=%lu|source=%lu|volume=%lu|initerr=%lu|mute=%lu|volume_pct=%lu|active=%lu|speaker=%lu\r\n",
     (unsigned long)g_g1_board.mic_running,(unsigned long)g_g1_board.mic_blocks,
     (unsigned long)g_g1_board.mic_peak,(unsigned long)g_g1_board.mic_overflow,
     (unsigned long)g_g1_board.mic_errors,(unsigned long)g_g1_board.touch_present,
     (unsigned long)g_g1_board.touch_reads,(unsigned long)g_g1_board.touch_errors,
     (unsigned long)g_g1_board.encoder_detents,
     (unsigned long)g_demo_control.requested_source_mode,
     (unsigned long)g_demo_control.output_gain_q15,
     (unsigned long)g_g1_board.init_error_bits,
     (unsigned long)s_product_ui.muted,
     (unsigned long)G1Ui_Percent(&s_product_ui),
     (unsigned long)g_demo_diag.active_source,
     (unsigned long)g_demo_diag.speaker_blocks_output);
    console_write(g_r21_line);return;
  }
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
  g_demo_control.audio_capture_enabled = 1U;
  g_demo_control.output_enabled = 1U; /* output muted by analog setup if downstream fails */
  g_demo_control.output_gain_q15 = G1_OUTPUT_MAX_Q15; /* UI 100%; limiter remains bounded. */
  g_demo_control.dsp_enabled = 0U;
  g_demo_control.evidence_enabled = 0U;
  g_demo_control.plot_enabled = 0U;
  /* Product's single speaker must hear BOTH input channels. Never default
   * to LEFT: doing so silenced right-only stereo test content in R15. */
  g_demo_control.bt_mono_mode = DEMO_BT_MONO_MIX;
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
  G1Pref_Init(&s_product_preferences);
  G1Ambient_Init(&s_ambient);
  G1Ui_Init(&s_product_ui);
  QrSleep_Init(&s_qr_sleep);
  QrDemoTest_Init(&s_test_generator);
  s_bt_sleep_transition=0U;
  s_audio_sleep_active=0U;
  s_test_saved_valid=0U;
  DhaEffects_Init();
  DhaFeedback_Request(DHA_CUE_BOOT);
  s_pref_amp=G1_PREF_QUIET;s_pref_spectrum=G1_PREF_LOW;
  /* Actual on-device online preference learning, initially neutral.
   * Explicit encoder adjustments are training labels, 9 bounded context bins.
   * Learned state is volatile (RAM) and does not boost above the output cap. */
  s_pref_enabled=1U;s_user_gain_q15=s_product_ui.gain_q15;
  g_demo_control.requested_source_mode=s_product_ui.source;
  g_demo_control.output_enabled=1U;
  product_apply_preference();
  GroupUi_Init(&g_group_ui);
  g_group_events = xQueueCreate(8U, sizeof(GroupEvent));
  if (g_group_events == NULL) Demo_SetError(0xF231U);
  dwt_enable();
  /* R26.6: preserve R26.4 LOW24 full-precision recovery, use a mild
   * continuous filter, and never auto-learn notches from programme audio.
   * An explicit SOURCE-OFF calibration may lock one static narrow notch. */
  s_tc_pcm_decode_mode = PCM_DECODE_LOW24;
  s_tc24_level_enabled = 1U;
  s_tc24_level_cap_q16 = 4U * 65536U;
  s_tc24_level_reset_requested = 0U;
  TcS24Recovery_Init(&s_tc24_level);
  s_tc_channel_mode = 0U; /* automatic left/right selection */
  g_r27_tc_clean_enabled = 1U; /* conservative linear filter only */
  s_tc_notch_auto_enabled = 0U;
  s_tc_filter_reset_requested = 0U;
  s_tc_hum_apply_hz=UINT32_MAX;
  s_tc_hum_last_hz=0U;
  TcHumCal_Init(&s_tc_hum_cal);
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

/* ============================================================================
 * FREERTOS TASK REGISTRY — priority and ownership
 * ============================================================================
 * AudioTask(7): audio deadlines + DSP. ControlTask(6): control/UART. BtTask(5):
 * receiver supervision. Background/CPULoadB(2): diagnostic stress workers.
 * HighLoad7(7) is a deliberately disruptive DEVELOPMENT-ONLY workload;
 * keep it inactive during a standalone hearing-aid demonstration.
 *
 * Do not add a second RTOS scheduler here. Task stacks are FreeRTOS stack
 * depth units; measure uxTaskGetStackHighWaterMark() on the real target before
 * assuming stack/RAM safety.
 */
static void create_child_tasks(void)
{
  BaseType_t a, b;
  if (g_children_started) return;
  a = xTaskCreate(StartAudioTask, "AudioTask", 1024U, NULL, 7U, &g_audio_task);
  b = xTaskCreate(StartBtTask, "BtTask", 512U, NULL, 5U, &g_bt_task);
#if DHA_ENABLE_RTOS_STRESS
  /* LAB ONLY: these three workers must not be created in operating image. */
  BaseType_t c = xTaskCreate(StartBackgroundTask, "Background", 384U, NULL, 2U, &g_background_task);
  BaseType_t d = xTaskCreate(StartCpuLoadB, "CPULoadB", 256U, NULL, 2U, &g_cpu_load_b_task);
  BaseType_t e = xTaskCreate(StartHighPriorityLoad, "HighLoad7", 256U, NULL, 7U, &g_r32_hi_task);
  if (c != pdPASS) Demo_SetError(0xF203U);
  if (d != pdPASS) console_write("WARN: CPULoadB lab worker unavailable\r\n");
  if (e != pdPASS) console_write("WARN: HighLoad7 lab worker unavailable\r\n");
  g_children_started = (a == pdPASS && b == pdPASS) ? 1U : 0U;
  console_write("RTOS: DEVELOPMENT image: Audio/BT and lab stress workers requested\r\n");
#else
  /* SYSTEM DEMO: no Background, CPULoadB or HighLoad7 task is created. */
  g_children_started = (a == pdPASS && b == pdPASS) ? 1U : 0U;
  console_write("RTOS: OPERATING image: AudioTask + BtTask; no dummy stress tasks\r\n");
#endif
  if (a != pdPASS) Demo_SetError(0xF201U);
  if (b != pdPASS) Demo_SetError(0xF202U);
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
  console_write("G1 STANDALONE PRODUCT: physical touch and encoder control, optional USART2 diagnostics\r\n");
  console_write("261013 CONNECTIVITY: real DMA/state-machine evidence; use !FA !FV !RM\r\n");
  console_write("QUIET RIOT: E8 BT/hold ROOM, E9 TC/hold 1k TEST, E10 MUTE/hold CHIRP, E11 MIC/hold NR; E8+E11 hold SLEEP, proximity WAKE\r\n");
  console_write("Standalone firmware active; UART: UI STATUS, SOURCE MIC|BT|TC|AUTO, MUTE ON|OFF, VOLUME 0..100.\r\n");
  product_ui_status();
  console_write("G1 R12: SYSTEM CHECK | MIC STATUS | MIC INIT | BOARD RAW | BT STATUS | WATCH ON|OFF\r\n");
  (void)snprintf(g_r21_line,sizeof(g_r21_line),
    "G1|MIC|BOOT|ready=%lu|stage=%lu|init_attempts=%lu|initerr=0x%lX\r\n",
    (unsigned long)g_g1_board.mic_ready,
    (unsigned long)g_g1_board.mic_init_stage,
    (unsigned long)g_g1_board.mic_init_attempts,
    (unsigned long)g_g1_board.init_error_bits);
  console_write(g_r21_line);
  if(g_g1_board.init_error_bits!=0U) {
    (void)snprintf(g_r21_line,sizeof(g_r21_line),
      "G1|WARN|board_init_error=0x%08lX|hardware_functions_degraded\r\n",
      (unsigned long)g_g1_board.init_error_bits);
    console_write(g_r21_line);
  }
  create_child_tasks();
  group_button_init();
  print_clock();
  g_last_stream_ms = HAL_GetTick();

  for (;;) {
    uint32_t now = HAL_GetTick();
#if G1_DIAGNOSTICS_UART_ENABLE
    poll_console();
#endif
    if(!s_qr_sleep.asleep) group_button_poll(now);
    product_ui_poll(now);
    if(G1_DIAGNOSTICS_UART_ENABLE && s_r10_watch_enabled &&
       (uint32_t)(now-s_r10_watch_last_ms)>=1000U){
       s_r10_watch_last_ms=now;r10_snapshot("LIVE");
    }
    update_task_rates(now);
    if(s_tc_hum_cal.state==TC_HUM_ANALYSING){
      if(TcHumCal_Step(&s_tc_hum_cal)) {
        if(s_tc_hum_cal.state==TC_HUM_DONE){
          s_tc_hum_last_hz=s_tc_hum_cal.best_hz;
          s_tc_hum_apply_hz=s_tc_hum_last_hz;
          (void)snprintf(g_r21_line,sizeof(g_r21_line),
              "G1|TC|HUM|LOCK|hz=%lu|spectral_fraction_permille=%lu|static_notch_only\r\n",
              (unsigned long)s_tc_hum_last_hz,
              (unsigned long)s_tc_hum_cal.peak_fraction_permille);
          console_write(g_r21_line);
        } else console_write("G1|TC|HUM|NO_STABLE_TONE|unchanged_audio\r\n");
      }
    }
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
    /* Human-verifiable BT link cue uses BT401 TS+ status, not PCM presence. */
    static uint8_t previous_link=0U;
    uint8_t linked=(uint8_t)(s_bt_status_code>=2U);
    if(!s_qr_sleep.asleep && linked != previous_link){
      DhaFeedback_Request(linked ? DHA_CUE_BT_CONNECTED : DHA_CUE_BT_DISCONNECTED);
    }
    previous_link=linked;
    g_demo_diag.control_task_loops++;
    g_demo_diag.control_stack_high_water = (uint32_t)uxTaskGetStackHighWaterMark(NULL);
    r29_refresh_debug_snapshot(now);
    g_demo_diag.uptime_ms = now;
    vTaskDelay(pdMS_TO_TICKS(2U)); /* UI encoder sampling; I2C remains 30ms rate-limited */
  }
}

/* REAL-TIME AUDIO PROCESSING
 * AudioTask owns the capture queues, routing service state and optional
 * DSP/guardian state. DMA interrupt callbacks only hand off completed PCM
 * blocks; avoid serial printing, filesystem writes and heap allocation here.
 * See Documentation/CODE_MAP.md for the related processing functions.
 *
 * DMA-to-task notifications remove idle tick polling (G5); this is not
 * yet a measured Cortex-M4 speedup. On-board latency and profiling remain
 * required validation gates.
 */
static void StartAudioTask(void *argument)
{
  (void)argument;
  uint32_t last_control_opportunity_ms=HAL_GetTick();
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

    /* Rebuild SAI master/slave capture after MIC/BT/TC/AUTO switching.
     * The original R17 g_dma_started flag prevented TC/BT receiver launch
     * following a successful microphone-only speaker session. */
    if (s_tc_restart_requested) {
      s_tc_restart_requested=0U;
      if(g_dma_started)stop_dma();
    }
    if (g_dma_started && g_dma_topology_mask != dma_desired_topology())
      stop_dma();

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
      if (g_demo_control.requested_source_mode != DEMO_MODE_MIC_ONLY &&
          (uint32_t)(now - g_last_state_step_ms) >= 20U) {
        step_service(now);
        g_last_state_step_ms = now;
      }

    }

    /* Local DFSDM microphone is a third genuine input, not simulated audio.
     * Drain other DMA snapshots for clock continuity but never render them in
     * MIC mode. Swap outputs at block boundaries via process_selected_audio. */
    if(s_mic_reinit_request) {
      s_mic_reinit_request=0U;
      if(g_g1_board.mic_running)(void)G1_MicStop();
      HAL_StatusTypeDef mi=G1_MicReinit();
      console_write(mi==HAL_OK?"G1|MIC|REINIT|HAL_OK\r\n":"G1|MIC|REINIT|HAL_ERROR|use_MIC_STATUS\r\n");
      s_mic_next_start_ms=now;
    }
    if (g_demo_control.requested_source_mode == DEMO_MODE_MIC_ONLY &&
        g_demo_control.audio_capture_enabled) {
      if (!g_g1_board.mic_running &&
          (int32_t)(now-s_mic_next_start_ms)>=0) {
        if (!g_g1_board.mic_ready) {
          if(G1_MicReinit()!=HAL_OK)Demo_SetError(0xA301U);
        }
        if(g_g1_board.mic_ready && G1_MicStart()!=HAL_OK)Demo_SetError(0xA302U);
        s_mic_next_start_ms=now+1000U; /* avoid 5-ms reinit storm */
      }
    } else if (g_g1_board.mic_running) {
      (void)G1_MicStop();
    }
    /* Drain immutable RX snapshots. The DMA ISR copied these blocks before the
     * circular DMA could reuse the memory, so block identity cannot be lost even
     * if this task is briefly delayed by diagnostics or another RTOS task. */
    {
      uint32_t handled = 0U;
      while (handled < (CAPTURE_RING_DEPTH - 1U) && capture_pop_bt(g_bt_work_words)) {
        if (g_demo_control.requested_source_mode != DEMO_MODE_MIC_ONLY)
          process_bt_words(g_bt_work_words, HAL_GetTick());
        handled++;
      }
      handled = 0U;
      while (handled < (CAPTURE_RING_DEPTH - 1U) && capture_pop_tc(g_tc_work_words)) {
        if (g_demo_control.requested_source_mode != DEMO_MODE_MIC_ONLY)
          process_tc_words(g_tc_work_words, HAL_GetTick());
        handled++;
      }
    }

    if (g_demo_control.requested_source_mode == DEMO_MODE_MIC_ONLY) {
      uint32_t handled=0U;
      while (handled++<6U && G1_MicTakeBlock(g_mono)) {
        g_demo_diag.active_source=3U;
        g_demo_diag.muted=s_product_ui.muted?1U:0U;
        memcpy(g_r27_pre_source,g_mono,sizeof(g_r27_pre_source));
        product_mic_condition(g_mono);
        process_selected_audio(g_mono,3U);
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
    /* DMA-to-task notifications replace idle tick polling. A bounded 5-ms
     * timeout maintains FSM/control servicing when there is no audio input. */
    if ((g_bt_cap_head != g_bt_cap_tail) || (g_tc_cap_head != g_tc_cap_tail)) {
      /* R26.2: taskYIELD() gives CPU only to tasks at the SAME priority.
       * At continuous 48-kHz TC DMA traffic, AudioTask(7) can therefore
       * starve the touch/encoder ControlTask(6) indefinitely. Once every
       * 8 ms, block exactly one RTOS tick to service hardware controls.
       * Capture queues remain bounded and are drained next iteration. */
      const uint32_t fairness_now=HAL_GetTick();
      if ((uint32_t)(fairness_now-last_control_opportunity_ms)>=8U) {
        last_control_opportunity_ms=fairness_now;
        vTaskDelay(pdMS_TO_TICKS(1U));
      } else taskYIELD();
    } else {
      last_control_opportunity_ms=HAL_GetTick();
      (void)ulTaskNotifyTake(pdTRUE,pdMS_TO_TICKS(5U));
    }
  }
}

static void StartBtTask(void *argument)
{
  (void)argument;
  uint32_t bluetooth_boot_ms=HAL_GetTick();
  uint8_t bluetooth_auto_discoverable_sent=0U;
  QrBtVolume volume_tracker;
  QrBtVolume_Init(&volume_tracker);
  uint32_t last_bt_status_poll_ms=0U;
  for (;;) {
    poll_bt_uart();
    if(s_bt_sleep_transition){
      uint8_t command=s_bt_sleep_transition;
      s_bt_sleep_transition=0U;
      if(command==1U){
        /* BT401: disconnect active EDR, stop playback, leave pairing mode.
         * UART commands are best effort; hardware BT power is not gated. */
        (void)bt_tx("AT+BA01\r\n",9U,NULL);
        (void)bt_tx("AT+B300\r\n",9U,NULL);
        (void)bt_tx("AT+B500\r\n",9U,NULL);
        (void)bt_tx("AT+CM08\r\n",9U,NULL);
        QrBtVolume_Init(&volume_tracker);s_bt_status_code=1U;
      }else if(command==2U){
        (void)bt_tx("AT+B501\r\n",9U,NULL);
        (void)bt_tx("AT+B301\r\n",9U,NULL);
        (void)bt_tx("AT+CM01\r\n",9U,NULL);
        bluetooth_auto_discoverable_sent=0U;
        bluetooth_boot_ms=HAL_GetTick();
        QrBtVolume_Init(&volume_tracker);
      }
    }
    if(s_qr_sleep.asleep){
      vTaskDelay(pdMS_TO_TICKS(20U));
      continue; /* no polling/profile/reconnect while sleeping */
    }
    {
      const uint8_t linked=(uint8_t)(s_bt_status_code>=2U);
      /* Re-arm after EVERY real TS+00 disconnect, then run on each TS+01
       * or TS+02 connection edge, including reconnections after sleep.
       * CA100 is user-requested and module-firmware dependent: the BT401
       * published manual documents only CA00..CA30. Never claim that the
       * handset's Android/iOS media slider has been modified. */
      if(QrBtVolume_ConnectedEdge(&volume_tracker,linked)) {
        if(bt_tx(QR_BT_CONNECT_VOLUME_COMMAND,
                 sizeof(QR_BT_CONNECT_VOLUME_COMMAND)-1U,NULL)) {
          (void)bt_tx(QR_BT_VOLUME_QUERY_COMMAND,
                      sizeof(QR_BT_VOLUME_QUERY_COMMAND)-1U,NULL);
        }
      }
      /* Query the real link periodically so the rising edge can be
       * rediscovered even if the module omits an unsolicited event. */
      const uint32_t bt_now=HAL_GetTick();
      if((uint32_t)(bt_now-last_bt_status_poll_ms)>=3000U) {
        last_bt_status_poll_ms=bt_now;
        (void)bt_tx("AT+TS\r\n",sizeof("AT+TS\r\n")-1U,NULL);
      }
    }
    /* On a standalone device the receiver should become discoverable at
     * power-up without a PC. Never report connected until the module replies. */
    if (!bluetooth_auto_discoverable_sent &&
        (uint32_t)(HAL_GetTick()-bluetooth_boot_ms)>=2000U) {
      bluetooth_auto_discoverable_sent=1U;
      if(s_bt_status_code<2U){
        /* DFRobot DFR0781 reference: AT+CM01 enters Bluetooth playback;
         * AT+B301 enables BT audio and AT+B501 enables EDR discovery.
         * Only AFTER the profile has been sent do we request BA06. */
        g_demo_control.apply_bt_profile_request=1U;
        s_bt_pair_request=1U;
        console_write("G1|BT|BOOT|PROFILE_THEN_DISCOVERABILITY|not_connected_yet\r\n");
      }
    }
    if(s_bt_recover_request){
      s_bt_recover_request=0U;
      (void)HAL_UART_AbortReceive(&huart1);
      HAL_StatusTypeDef u=HAL_UART_Receive_IT(&huart1,&g_bt_rx_irq_byte,1U);
      g_demo_diag.bt_rx_irq_armed=(uint32_t)(u==HAL_OK);
      console_write(u==HAL_OK?"G1|BT|RECOVER|RX_ARMED\r\n":"G1|BT|RECOVER|RX_FAILED\r\n");
      bt_query();
    }
    if (s_bt_pair_check_due!=0U && (int32_t)(HAL_GetTick()-s_bt_pair_check_due)>=0){
      s_bt_pair_check_due=0U;
      static const uint8_t query[]="AT+TS\r\n";
      (void)HAL_UART_Transmit(&huart1,(uint8_t*)query,sizeof(query)-1U,250U);
      console_write("G1|BT|PAIR|STATUS_QUERY_SENT|look_for_TS_response\r\n");
    }
    if (s_r10_bt_at_pending) {
      char command[sizeof(s_r10_bt_at_line)];
      uint16_t length=(uint16_t)strlen(s_r10_bt_at_line);
      memcpy(command,s_r10_bt_at_line,length+1U);
      s_r10_bt_at_pending=0U;
      if(HAL_UART_Transmit(&huart1,(uint8_t *)command,length,250U)==HAL_OK){
        g_demo_diag.bt_uart_tx_bytes+=length;
        console_write("G1|BT|AT|UART_SENT|not_yet_acknowledged\r\n");
      }else console_write("G1|BT|AT|UART_FAIL\r\n");
    }
    if (g_demo_control.apply_bt_profile_request) {
      BtProfileConfig_t cfg = BtProfile_DefaultConfig();
      size_t sent = 0U;
      BtProfileResult_t r;
      g_demo_diag.bt_profile_attempts++;
      r = BtProfile_Apply(&cfg, bt_tx, bt_delay, NULL, &sent);
      g_demo_diag.bt_profile_commands_sent = (uint32_t)sent;
      if (r == BT_PROFILE_OK) {
        g_demo_diag.bt_profile_successes++; /* UART sends only, NOT module ACK */
        console_write("G1|BT|PROFILE|UART_SEQUENCE_SENT|await_RX_ack_and_phone\r\n");
      } else console_write("G1|BT|PROFILE|UART_TX_FAILED\r\n");
      g_demo_control.apply_bt_profile_request = 0U;
    }
    if (s_bt_pair_request) {
      if(g_demo_diag.bt_rx_irq_armed==0U){
        (void)HAL_UART_AbortReceive(&huart1);
        if(HAL_UART_Receive_IT(&huart1,&g_bt_rx_irq_byte,1U)==HAL_OK)
          g_demo_diag.bt_rx_irq_armed=1U;
      }
      /* Documented KT1026A BT401 v1.6 audio discoverability request.
       * UART success is not proof that a phone paired or audio PCM arrived. */
      static const uint8_t pair_cmd[]="AT+BA06\r\n";
      s_bt_pair_request=0U;
      if (HAL_UART_Transmit(&huart1,(uint8_t*)pair_cmd,
           (uint16_t)(sizeof(pair_cmd)-1U),250U)==HAL_OK) {
        g_demo_diag.bt_uart_tx_bytes+=(uint32_t)(sizeof(pair_cmd)-1U);
        s_bt_pair_tx_ms=HAL_GetTick();
        s_bt_pair_check_due=s_bt_pair_tx_ms+500U;
        console_write("G1|BT|PAIR|UART_SENT|NOT_YET_CONFIRMED|await_BT_RX_and_phone\r\n");
      } else console_write("G1|BT|PAIR|UART_FAIL|check_3V3_and_UART1\r\n");
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

static void __attribute__((unused)) StartBackgroundTask(void *argument)
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
static void __attribute__((unused)) StartCpuLoadB(void *argument)
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
/* SCHEDULER CONTENTION EXPERIMENT — NOT PRODUCT OPERATION.
 * Activation intentionally busy-spins at AudioTask priority 7 for ~20 s.
 * ControlTask, Bluetooth supervision and the UART dashboard may starve.
 * Do not enable this in a wearable product or the standalone group showcase.
 */
static void __attribute__((unused)) StartHighPriorityLoad(void *argument)
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
