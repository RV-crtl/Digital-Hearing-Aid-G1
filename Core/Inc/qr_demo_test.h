#ifndef QR_DEMO_TEST_H
#define QR_DEMO_TEST_H
#include <stddef.h>
#include <stdint.h>
/* Deliberate high-output BENCH signal only; not calibrated to acoustic power.
 * Normal user listening still obeys G1_UI_MAX_GAIN_Q15 and other limits.
 * Compile-time ceiling leaves headroom for the hardware signal path.
 * Modes: 0 off, 1 1 kHz, 2 2 kHz, 3 3 kHz, 4 repeating 250..4000 Hz chirp.
 * Never run against a user's head/ear until acoustic output is measured. */
#define QR_TEST_PEAK_PCM 32200
#define QR_TEST_CHIRP_SAMPLES 96000U
#define QR_TEST_SAMPLE_RATE 48000U
#define QR_TEST_RAMP_INCREMENT_Q15 40U /* ~17 ms nominal attack/release */
typedef struct {uint32_t phase_q16,chirp_sample,gain_q15;uint8_t last_mode,playing_mode;} QrDemoTest;
void QrDemoTest_Init(QrDemoTest *s);
uint8_t QrDemoTest_IsActive(const QrDemoTest *s);
void QrDemoTest_Render(QrDemoTest *s,uint8_t mode,int16_t *dst,size_t frames);
#endif
