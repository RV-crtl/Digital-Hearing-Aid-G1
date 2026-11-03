/**
 * @file tc_cleanup.c
 * @brief Adaptive induction-loop noise control; preserves LOW16 PCM decoding.
 *
 * The analogue front end can contribute idle hiss even with no loop signal.
 * This processor uses two LP poles (~3.2 kHz), a 160 Hz HP pole, a bounded
 * up to 6x slow signal-level recovery, and an activity-controlled *soft* idle-noise floor.
 * The activity estimate combines RMS above a slowly updated noise baseline
 * with lag-1 correlation (coherent speech/tone vs uncorrelated hiss).
 * It is not a validated voice-activity detector and cannot remove in-band
 * interference, bad solder joints or ADC noise without harming weak signals.
 */
#include "tc_cleanup.h"
#include <string.h>
#include <math.h>
#include <stdint.h>

#define TC_PEAK 27000.0f
void TcCleanup_Reset(TcCleanup *st){
  if(!st)return;
  memset(st,0,sizeof(*st));
  st->noise_floor_rms=65U;
  st->gate_gain=0.05f;
  st->signal_gain=2.7f;
}
void TcCleanup_Block(TcCleanup *st,const int16_t *in,int16_t *out,size_t n){
  if(!st||!in||!out||!n)return;
  uint64_t power=0U;
  int64_t correlation=0;
  for(size_t i=0;i<n;i++){
    int32_t x=in[i];
    power+=(uint32_t)(x*x);
    if(i)correlation+=(int32_t)x*(int32_t)in[i-1U];
  }
  /* Fixed-size, bounded integer square root using math library's hardware FPU.
   * Correlation is a feature for soft noise-suppression, not signal origin ID. */
  uint32_t rms=(uint32_t)sqrtf((float)(power/n));
  st->last_input_rms=rms;
  const uint32_t corr=(correlation>0 && power>0U)?
    (uint32_t)(((uint64_t)correlation*32768U)/(power)):0U;
  st->last_corr_q15=(corr>32768U)?32768U:corr;
  const uint32_t floor=st->noise_floor_rms;
  const uint32_t threshold=((floor*3U/2U+30U)>100U)?(floor*3U/2U+30U):100U;
  /* Coherent, low-level 2-kHz signals should open even with recent idle hiss.
   * A high-correlation shortcut is not a 'speech classifier'. */
  bool activity=((rms>75U) && (st->last_corr_q15>22000U)) ||
                ((rms>threshold) && (st->last_corr_q15>15000U));
  if(activity){st->hold_blocks=48U;st->active_blocks++;}
  else if(st->hold_blocks>0U)st->hold_blocks--;
  else st->quiet_blocks++;
  if(!activity){
    /* Never learn the full unwanted signal immediately, and cap baseline so
     * the empirically observed ~800-RMS wanted tone can still reopen gate. */
    if(rms<st->noise_floor_rms)
      st->noise_floor_rms=(31U*st->noise_floor_rms+rms)/32U;
    else if(rms<1000U)
      st->noise_floor_rms=(511U*st->noise_floor_rms+rms)/512U;
    if(st->noise_floor_rms>300U)st->noise_floor_rms=300U;
    if(st->noise_floor_rms<16U)st->noise_floor_rms=16U;
  }
  const float target=(activity||st->hold_blocks)?1.0f:0.05f;
  /* Gain up ~10-20ms, down ~200ms: no abrupt gate open/close clicks. */
  const float smooth=target>st->gate_gain?0.20f:0.010f;
  st->gate_gain+=smooth*(target-st->gate_gain);
  /* Closed-loop gain recovery is independent of the user's volume control.
   * Weak coherent audio is made more audible; strong input rapidly backs off.
   * The hard limiter and global bounded output control remain authoritative.
   * A ~100 ms upward time constant avoids syllabic pumping. */
  float wanted_gain=(rms>0U)?4800.0f/(float)rms:2.7f;
  if(wanted_gain>6.0f)wanted_gain=6.0f;
  if(wanted_gain<1.0f)wanted_gain=1.0f;
  if(wanted_gain<st->signal_gain)
    st->signal_gain+=0.18f*(wanted_gain-st->signal_gain);
  else
    st->signal_gain+=0.020f*(wanted_gain-st->signal_gain);
  const float hp_alpha=0.02073f; /* 160 Hz */
  const float lp_alpha=0.34228f; /* 3200 Hz x 2 poles */
  uint64_t out_energy=0U;
  for(size_t i=0;i<n;i++){
    float x=(float)in[i];
    st->hp_lp+=hp_alpha*(x-st->hp_lp);
    float y=x-st->hp_lp;
    st->lp+=lp_alpha*(y-st->lp);
    st->lp2+=lp_alpha*(st->lp-st->lp2);
    y=st->lp2*(st->signal_gain*st->gate_gain);
    /* Output limiter is ALWAYS active, independent of user gain. */
    if(y>TC_PEAK){y=TC_PEAK;st->limiter_samples++;}
    if(y< -TC_PEAK){y=-TC_PEAK;st->limiter_samples++;}
    out[i]=(int16_t)y;
    out_energy+=(uint32_t)((int32_t)out[i]*(int32_t)out[i]);
  }
  st->last_output_rms=(uint32_t)sqrtf((float)(out_energy/n));
}
