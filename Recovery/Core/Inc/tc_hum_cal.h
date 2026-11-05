/** @file tc_hum_cal.h
 * @brief One-shot, explicit, source-OFF telecoil interference calibration.
 * Capture is fed by AudioTask; spectral analysis executes incrementally in
 * ControlTask. No adaptive frequency tracking while programme audio plays.
 */
#ifndef TC_HUM_CAL_H
#define TC_HUM_CAL_H
#include <stddef.h>
#include <stdint.h>
#define TC_HUM_CAL_SAMPLES 2048U
#define TC_HUM_CAL_DECIMATE 4U
#define TC_HUM_CAL_FS 12000U
#define TC_HUM_CAL_START_HZ 75U
#define TC_HUM_CAL_STOP_HZ 4500U
typedef enum {
 TC_HUM_IDLE=0, TC_HUM_CAPTURE_REQUESTED=1, TC_HUM_CAPTURING=2,
 TC_HUM_ANALYSING=3, TC_HUM_DONE=4, TC_HUM_FAILED=5
} TcHumCalState;
typedef struct {
 volatile uint32_t state;
 uint32_t count, decim_count;
 int32_t decim_accum;
 int16_t pcm[TC_HUM_CAL_SAMPLES];
 int64_t sum, sum_sq;
 float total_power, max_coarse_power, max_fine_power, coarse_sum_power;
 uint32_t best_coarse_hz, best_hz, peak_fraction_permille;
 uint32_t bin_index, bins_measured;
 uint8_t stage; /* 0 coarse, 1 fine */
} TcHumCal;
void TcHumCal_Init(TcHumCal *s);
void TcHumCal_Request(TcHumCal *s);
void TcHumCal_Feed(TcHumCal *s, const int16_t *samples, size_t n);
/* One frequency-bin evaluated per call on ControlTask; returns true once done. */
int TcHumCal_Step(TcHumCal *s);
#endif
