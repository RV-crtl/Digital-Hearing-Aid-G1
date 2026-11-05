/**
 * @file tc_idle_squelch.h
 * @brief Quiet Riot QR261104.6: single-gain, slow telecoil idle attenuation.
 *
 * The PCM waveform and filters remain those of QR261104.3. This module
 * measures level and short-lag correlation to distinguish stationary noise
 * from weak tonal programme. Only ONE gain is applied to the whole block.
 * There is no spectral subtraction, adaptive band gain or hard noise gate.
 *
 * LIMIT: no algorithm can reliably distinguish wanted audio from noise at
 * identical amplitude/spectrum. This does NOT improve analogue input SNR.
 * Own the state in AudioTask and call exactly once per 128-sample TC block.
 */
#ifndef TC_IDLE_SQUELCH_H
#define TC_IDLE_SQUELCH_H
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

typedef struct {
    uint32_t noise_rms;
    uint32_t quiet_min_rms;
    uint32_t last_rms;
    uint32_t gain_q15;
    uint32_t calibrating_blocks;
    uint32_t activity_streak;
    uint32_t hold_blocks;
    uint32_t quiet_blocks;
    uint32_t open_blocks;
    uint32_t detected_events;
    uint32_t blocks_processed;
    uint32_t calibration_count;
    int32_t corr16_smooth_milli;
    int32_t corr48_smooth_milli;
    uint16_t corr16_milli;
    uint16_t corr48_milli;
    uint8_t enabled;
    uint8_t learned;
} TcIdleSquelch;

void TcIdleSquelch_Init(TcIdleSquelch *s);
/* Keeps configuration while clearing old signal/noise history. */
void TcIdleSquelch_Relearn(TcIdleSquelch *s);
/* No effect on PDM microphone, Bluetooth, speaker cues, or test generators. */
void TcIdleSquelch_Enable(TcIdleSquelch *s, bool enabled);
/* In-place process; requires n=128 in operating firmware, works for n>=49. */
void TcIdleSquelch_Process(TcIdleSquelch *s, int16_t *samples, size_t n);
#endif
