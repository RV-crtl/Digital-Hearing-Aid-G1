/** Telecoil-only noise treatment. AudioTask owns all state. No dynamic memory. */
#ifndef TC_CLEANUP_H
#define TC_CLEANUP_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
typedef struct {
  float hp_lp, lp, lp2, envelope, gate_gain, signal_gain;
  uint32_t noise_floor_rms, last_input_rms, last_output_rms, last_corr_q15;
  uint32_t hold_blocks, active_blocks, quiet_blocks, limiter_samples;
} TcCleanup;
void TcCleanup_Reset(TcCleanup *st);
void TcCleanup_Block(TcCleanup *st,const int16_t *input,int16_t *output,size_t n);
#endif
