/**
 * @file tc_s24_recovery.h
 * @brief Telecoil signed-24-bit capture with bounded, transparent level recovery.
 *
 * PCM1808/SAI still supplies 24-bit words in 32-bit DMA storage. Conversion
 * to 16-bit happens only AFTER level control, avoiding the R26.3 >>8 truncation
 * when the captured ADC words contain a low-amplitude but valid signal.
 * Not a noise eliminator; neither gates nor spectral notches are applied.
 */
#ifndef TC_S24_RECOVERY_H
#define TC_S24_RECOVERY_H
#include <stddef.h>
#include <stdint.h>

typedef struct {
    uint32_t gain_q16;          /* s16 codes per raw24 count, Q16 */
    uint32_t cap_q16;           /* maximum digital gain, Q16 */
    uint32_t left_rms24;
    uint32_t right_rms24;
    uint32_t max_peak24;
    uint32_t out_rms16;
    uint32_t clipped_samples;
    uint32_t processed_blocks;
} TcS24Recovery;

void TcS24Recovery_Init(TcS24Recovery *s);
void TcS24Recovery_SetCap(TcS24Recovery *s, uint32_t cap_q16);
/* Returns 0 for invalid buffers, 1 for successful stereo extraction.
 * There is NO input-level threshold and NO gating of wanted audio. */
int TcS24Recovery_Process(TcS24Recovery *s, const uint32_t *interleaved_words,
                          size_t frame_count, int16_t *left, int16_t *right);
#endif
