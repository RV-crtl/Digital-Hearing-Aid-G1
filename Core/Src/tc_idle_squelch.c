/** @file tc_idle_squelch.c
 * QR261104.6: silence comfort for manual TC without voice-muffling DSP.
 *
 * - Noise reference is formed from short, noise-like blocks at acquisition;
 *   periodic programme during entry cannot train the noise floor.
 * - Two low-cost coherence measurements distinguish weak 0.5/1/2-kHz
 *   programme from stochastic noise / 100-240 Hz rumble. Neither controls
 *   a spectral filter; both affect a SINGLE smooth scalar gain.
 * - 13 ms candidate hysteresis, ~0.4 s programme hold, ~0.4 s fade to -30 dB.
 * - No allocations, locks, task delay, transcendental math or FFT.
 *
 * No uncalibrated DSP can reject analogue noise under an equally weak
 * wanted signal: use source-OFF ADC capture for further hardware diagnosis.
 */
#include "tc_idle_squelch.h"
#include <limits.h>
#include <string.h>

#define TC_IDLE_FULL_GAIN       32768U
#define TC_IDLE_MIN_GAIN         1024U /* -30.1 dB at idle, no hard zero */
#define TC_IDLE_CAL_BLOCKS         94U /* ~0.25 s at 48 kHz / 128 */
#define TC_IDLE_HOLD_BLOCKS      150U /* 0.4 seconds, programme tail preserved */
#define TC_IDLE_OPEN_STREAK        5U /* ~13.3 ms */
#define TC_IDLE_TONE_STREAK        4U /* ~10.7 ms */
#define TC_IDLE_CORRELATION_LAG1  16U
#define TC_IDLE_CORRELATION_LAG2  48U

static uint32_t root_u64(uint64_t x)
{
    uint64_t bit=UINT64_C(1)<<62, result=0U;
    while(bit>x)bit>>=2;
    while(bit) {
        if(x>=result+bit){x-=result+bit;result=(result>>1)+bit;}
        else result>>=1;
        bit>>=2;
    }
    return (uint32_t)result;
}

/* Signed normalised autocorrelation in units of 1/1000, energy-normalised.
 * Two 64-bit divisions per 128-sample block; no float in the detector. */
static int32_t coherence(const int16_t *x,size_t n,size_t lag)
{
    if(n<=lag)return 0;
    int64_t dot=0;
    uint64_t en0=0,en1=0;
    for(size_t i=lag;i<n;++i){
        const int64_t a=x[i],b=x[i-lag];
        dot+=a*b;en0+=(uint64_t)(a*a);en1+=(uint64_t)(b*b);
    }
    const uint64_t den=(en0+en1)/2U;
    if(den==0U)return 0;
    const uint64_t magnitude=(uint64_t)(dot<0?-dot:dot);
    const uint32_t m=(uint32_t)(magnitude*1000U/den);
    const int32_t c=(int32_t)(m>1000U?1000U:m);
    return dot<0?-c:c;
}

void TcIdleSquelch_Init(TcIdleSquelch *s)
{
    if(!s)return;
    memset(s,0,sizeof(*s));
    s->enabled=1U;
    s->gain_q15=TC_IDLE_FULL_GAIN;
    s->quiet_min_rms=UINT32_MAX;
}

void TcIdleSquelch_Relearn(TcIdleSquelch *s)
{
    if(!s)return;
    const uint8_t enabled=s->enabled;
    const uint32_t previous=s->calibration_count;
    TcIdleSquelch_Init(s);
    s->enabled=enabled;
    s->calibration_count=previous+1U;
}

void TcIdleSquelch_Enable(TcIdleSquelch *s,bool enabled)
{
    if(!s)return;
    s->enabled=enabled?1U:0U;
    if(!enabled){s->gain_q15=TC_IDLE_FULL_GAIN;s->hold_blocks=0U;s->activity_streak=0U;}
}

void TcIdleSquelch_Process(TcIdleSquelch *s,int16_t *samples,size_t n)
{
    if(!s||!samples||n==0U)return;
    if(!s->enabled){s->gain_q15=TC_IDLE_FULL_GAIN;return;}
    uint64_t sum=0U;
    for(size_t i=0U;i<n;++i){const int64_t v=samples[i];sum+=(uint64_t)(v*v);}
    const uint32_t rms=root_u64(sum/n);
    const int32_t c16=coherence(samples,n,TC_IDLE_CORRELATION_LAG1);
    const int32_t c48=coherence(samples,n,TC_IDLE_CORRELATION_LAG2);
    /* Correlation averages prevent sporadic noise blocks opening the path. */
    const uint32_t abs48=(uint32_t)(s->corr48_smooth_milli<0?-s->corr48_smooth_milli:s->corr48_smooth_milli);
    s->last_rms=rms;
    s->corr16_smooth_milli+=(c16-s->corr16_smooth_milli)/8;
    s->corr48_smooth_milli+=(c48-s->corr48_smooth_milli)/8;
    s->corr16_milli=(uint16_t)(s->corr16_smooth_milli+1000);
    s->corr48_milli=(uint16_t)(s->corr48_smooth_milli+1000);
    ++s->blocks_processed;

    /* Exclude coherent tones from startup idle-learning; a weak constant
     * 1-kHz stimulus must play if it was already on when TC was selected. */
    const bool tone=(n>TC_IDLE_CORRELATION_LAG2 &&
                     abs48>120U && s->corr16_smooth_milli<720 && rms>=24U);
    if(!s->learned) {
        /* Allow correlation EMA to settle before accepting a noise reference. */
        if(s->calibrating_blocks>16U && !tone && rms<s->quiet_min_rms)
            s->quiet_min_rms=rms;
        if(++s->calibrating_blocks>=TC_IDLE_CAL_BLOCKS){
            if(s->quiet_min_rms==UINT32_MAX){
                /* Programme was already on: don't train to its amplitude. */
                s->noise_rms=rms/4U;
            }else{
                s->noise_rms=s->quiet_min_rms;
            }
            if(s->noise_rms<12U)s->noise_rms=12U;
            s->learned=1U;
            s->activity_streak=0U;
            s->hold_blocks=TC_IDLE_HOLD_BLOCKS;
        }
    }

    uint8_t is_active=0U;
    if(s->learned){
        const uint32_t baseline=s->noise_rms;
        /* Compare level to a remembered noise-like baseline. Thresholds
         * favour audibility; a faint coherent tone may reopen below +3 dB. */
        const bool energy=(rms>baseline+baseline/3U+24U);
        const bool weak_tone=tone && rms>baseline+baseline/12U+4U;
        const bool candidate=energy||weak_tone;
        if(candidate){
            if(s->activity_streak<TC_IDLE_OPEN_STREAK)s->activity_streak++;
        }else s->activity_streak=0U;
        is_active=(uint8_t)(s->activity_streak >=
                           (weak_tone?TC_IDLE_TONE_STREAK:TC_IDLE_OPEN_STREAK));
        if(is_active){
            if(s->hold_blocks==0U)++s->detected_events;
            s->hold_blocks=TC_IDLE_HOLD_BLOCKS;
            ++s->open_blocks;
        }else if(s->hold_blocks>0U){
            --s->hold_blocks;
        }else{
            ++s->quiet_blocks;
            /* Track slow environmental noise variation only while truly
             * idle. Never learn an active programme as a noise baseline. */
            if(!candidate){
                if(rms<s->noise_rms) s->noise_rms=(31U*s->noise_rms+rms)/32U;
                else if(rms>s->noise_rms && (s->quiet_blocks&31U)==0U)
                    ++s->noise_rms;
                if(s->noise_rms<12U)s->noise_rms=12U;
            }
        }
    }

    const uint32_t desired=(!s->learned||s->hold_blocks>0U)?
                           TC_IDLE_FULL_GAIN:TC_IDLE_MIN_GAIN;
    const uint32_t initial=s->gain_q15;
    uint32_t final=initial;
    if(desired>initial){
        /* ~8 ms recovery once a signal is recognised, avoiding clicks. */
        final=initial+(desired-initial+2U)/3U;
        if(final>desired)final=desired;
    }else if(desired<initial){
        /* Fixed slew: approximately 0.4 s from unity to the quiet floor.
         * Linear ramp inside each 128-sample block avoids hard gating. */
        const uint32_t step=(TC_IDLE_FULL_GAIN-TC_IDLE_MIN_GAIN+149U)/150U;
        final=initial > step ? initial-step : desired;
        if(final<desired)final=desired;
    }
    s->gain_q15=final;
    if(initial==TC_IDLE_FULL_GAIN&&final==TC_IDLE_FULL_GAIN)return;
    /* Linear block ramp. The same gain multiplies EVERY frequency band. */
    const int32_t delta=(int32_t)final-(int32_t)initial;
    for(size_t i=0U;i<n;++i){
        const int32_t g=(int32_t)initial+(int32_t)((int64_t)delta*(int64_t)(i+1U)/(int64_t)n);
        const int32_t y=(int32_t)((int64_t)samples[i]*(int64_t)g/TC_IDLE_FULL_GAIN);
        samples[i]=(int16_t)y;
    }
}
