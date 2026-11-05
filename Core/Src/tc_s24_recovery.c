/** @file tc_s24_recovery.c */
#include "tc_s24_recovery.h"
#include "pcm_transport.h"
#include <limits.h>

#define TC24_UNITY_Q16 65536U
#define TC24_TARGET_RMS 1600U     /* conservative digital target, NOT acoustic SPL */
#define TC24_MAX_OUTPUT_PEAK 27000U /* PCM headroom before speaker/UI gain */
#define TC24_MIN_TRACK_RMS 64U    /* do not raise the noise floor during silence */
#define TC24_DEFAULT_CAP_Q16 (4U * TC24_UNITY_Q16)

static uint32_t sqrt_u64(uint64_t value) {
    uint64_t bit = UINT64_C(1) << 62, out = 0;
    while (bit > value) bit >>= 2;
    while (bit) {
        if (value >= out + bit) { value -= out + bit; out = (out >> 1) + bit; }
        else out >>= 1;
        bit >>= 2;
    }
    return (uint32_t)out;
}

static int32_t sign_extend_s24(uint32_t word) {
    uint32_t v = word & UINT32_C(0x00FFFFFF);
    if (v & UINT32_C(0x00800000)) v |= UINT32_C(0xFF000000);
    return (int32_t)v;
}

void TcS24Recovery_Init(TcS24Recovery *s) {
    if (!s) return;
    *s = (TcS24Recovery){0};
    s->gain_q16 = TC24_UNITY_Q16; /* raw low-amplitude values remain audible */
    s->cap_q16 = TC24_DEFAULT_CAP_Q16;
}
void TcS24Recovery_SetCap(TcS24Recovery *s, uint32_t cap_q16) {
    if (!s) return;
    if (cap_q16 < TC24_UNITY_Q16) cap_q16 = TC24_UNITY_Q16;
    if (cap_q16 > 8U * TC24_UNITY_Q16) cap_q16 = 8U * TC24_UNITY_Q16;
    s->cap_q16 = cap_q16;
    if (s->gain_q16 > s->cap_q16) s->gain_q16 = s->cap_q16;
}

int TcS24Recovery_Process(TcS24Recovery *s, const uint32_t *words,
                          size_t n, int16_t *left, int16_t *right) {
    if (!s || !words || !left || !right || n == 0U || n > INT32_MAX) return 0;
    uint64_t l2 = 0U, r2 = 0U, rendered2 = 0U;
    uint32_t peak = 0U, clips = 0U;
    for (size_t i = 0U; i < n; i++) {
        int32_t l = sign_extend_s24(words[i * 2U]);
        int32_t r = sign_extend_s24(words[i * 2U + 1U]);
        uint32_t al = l < 0 ? (uint32_t)(-(int64_t)l) : (uint32_t)l;
        uint32_t ar = r < 0 ? (uint32_t)(-(int64_t)r) : (uint32_t)r;
        if (al > peak) peak = al;
        if (ar > peak) peak = ar;
        l2 += (uint64_t)((int64_t)l * l);
        r2 += (uint64_t)((int64_t)r * r);
    }
    const uint32_t lrms = sqrt_u64(l2 / n), rrms = sqrt_u64(r2 / n);
    const uint32_t level = lrms > rrms ? lrms : rrms;
    uint32_t desired = TC24_UNITY_Q16;
    if (level >= TC24_MIN_TRACK_RMS) {
        uint64_t calc = ((uint64_t)TC24_TARGET_RMS << 16) / level;
        desired = calc > UINT32_MAX ? UINT32_MAX : (uint32_t)calc;
    }
    if (desired > s->cap_q16) desired = s->cap_q16;
    if (!desired) desired = 1U;
    /* Immediate peak guard, also on FIRST block after switching source.
     * A real full-scale S24 sample must be attenuated, never clipped to S16. */
    uint32_t safe = s->cap_q16;
    if (peak > 0U) {
        uint64_t calc = ((uint64_t)TC24_MAX_OUTPUT_PEAK << 16) / peak;
        safe = calc > UINT32_MAX ? UINT32_MAX : (uint32_t)calc;
        if (!safe) safe = 1U;
    }
    if (desired > safe) desired = safe;
    uint32_t start = s->gain_q16 ? s->gain_q16 : TC24_UNITY_Q16;
    if (start > safe) start = safe;
    if (start > s->cap_q16) start = s->cap_q16;
    uint32_t target = desired;
    if (target > start) {
        /* Attack/recovery: ~40 ms for a 16x step at 48 kHz / 128 frames.
         * No pumping or instant multiplication of idle noise. */
        uint32_t delta = target - start;
        target = start + ((delta + 15U) / 16U);
    } else {
        /* Fast downward gain to protect from an unexpectedly strong tone. */
        target = (start + target) / 2U;
    }
    if (target > safe) target = safe;
    /* Integer-ramp (Bresenham) gain interpolation: two divisions PER BLOCK,
     * no software 64-bit division in the 48-kHz per-sample audio loop. */
    const int32_t change = (int32_t)target - (int32_t)start;
    const int32_t denom = (int32_t)n;
    const int32_t step = change / denom;
    const int32_t rem = change % denom;
    int32_t gain_now = (int32_t)start, error = 0;
    for (size_t i = 0U; i < n; i++) {
        gain_now += step;
        error += rem;
        if (error >= denom) { gain_now++; error -= denom; }
        else if (error <= -denom) { gain_now--; error += denom; }
        const uint32_t gain = (uint32_t)gain_now;
        const int32_t l = sign_extend_s24(words[2U*i]);
        const int32_t r = sign_extend_s24(words[2U*i + 1U]);
        int64_t wl = ((int64_t)l * (int64_t)gain) / 65536LL;
        int64_t wr = ((int64_t)r * (int64_t)gain) / 65536LL;
        if (wl > INT16_MAX) { wl = INT16_MAX; clips++; }
        if (wl < INT16_MIN) { wl = INT16_MIN; clips++; }
        if (wr > INT16_MAX) { wr = INT16_MAX; clips++; }
        if (wr < INT16_MIN) { wr = INT16_MIN; clips++; }
        left[i] = (int16_t)wl;
        right[i] = (int16_t)wr;
        /* mean stereo energy for clear before/after probe values */
        rendered2 += (uint64_t)(wl*wl + wr*wr)/2U;
    }
    s->gain_q16 = target;
    s->left_rms24 = lrms;
    s->right_rms24 = rrms;
    s->max_peak24 = peak;
    s->out_rms16 = sqrt_u64(rendered2/n);
    s->clipped_samples += clips;
    s->processed_blocks++;
    return 1;
}
