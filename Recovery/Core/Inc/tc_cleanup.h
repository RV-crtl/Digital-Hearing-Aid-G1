/** @file tc_cleanup.h
 * @brief Stable telecoil speech-band filter and optional explicitly calibrated
 * fixed-frequency notch. No activity-gate, automatic notch hunting, or AGC.
 * Owned exclusively by AudioTask; legacy diagnostics preserved for display.
 */
#ifndef TC_CLEANUP_H
#define TC_CLEANUP_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
typedef struct {
  float hp_lp, hp_lp2, lp, lp2;
  float activity_low_lp, envelope, gate_gain, signal_gain;
  uint32_t noise_floor_rms, noise_high_rms, last_input_rms, last_output_rms;
  uint32_t last_corr_q15, last_high_rms, low_tone_blocks;
  uint32_t hold_blocks, active_blocks, quiet_blocks, limiter_samples;
  uint32_t zc_window_samples, zc_window_edges, tone_zc_hz;
  int16_t last_zc_sample;
  uint32_t candidate_edges, candidate_windows, candidate_last_hz;
  uint32_t notch_hz, notch_locks;
  uint8_t notch_auto_enabled, notch_window_ready;
  float notch_mix, notch_b0, notch_b1, notch_b2, notch_a1, notch_a2;
  float notch_x1, notch_x2, notch_y1, notch_y2;
} TcCleanup;
void TcCleanup_Reset(TcCleanup *st);
/* Historic name retained: switches a previously calibrated notch on/off.
 * No frequency is learned from active programme audio. */
void TcCleanup_SetNotchAuto(TcCleanup *st, bool enabled);
void TcCleanup_SetNotchFrequency(TcCleanup *st, uint32_t hz);
void TcCleanup_Block(TcCleanup *st, const int16_t *input, int16_t *output, size_t n);
#endif
