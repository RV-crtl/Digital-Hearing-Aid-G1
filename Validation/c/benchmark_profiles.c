/* Portable group DSP microbenchmark. NOT a Cortex-M4 cycle measurement.
 * Output is informational only; never use a timing threshold as a host-CI gate.
 * Measure neutral/unity versus voice attenuation and wind filtering.
 */
#if !defined(_WIN32)
#define _POSIX_C_SOURCE 200809L
#endif
#include "group_features.h"
#include <stdint.h>
#include <stdio.h>
#if defined(_WIN32)
#include <windows.h>
#else
#include <time.h>
#endif

#define FRAMES 128U
#define BLOCKS 60000U
static int16_t samples[FRAMES];
static int16_t output[FRAMES];
static volatile uint32_t checksum;

/* QPC on native Windows, POSIX clock otherwise. Host timing only. */
static uint64_t monotonic_ns(void) {
#if defined(_WIN32)
    LARGE_INTEGER counter, frequency;
    if (!QueryPerformanceFrequency(&frequency) || frequency.QuadPart <= 0 ||
        !QueryPerformanceCounter(&counter)) return 0;
    return (uint64_t)((double)counter.QuadPart * 1.0e9 /
                      (double)frequency.QuadPart);
#else
    struct timespec t;
    if (clock_gettime(CLOCK_MONOTONIC, &t) != 0) return 0;
    return ((uint64_t)t.tv_sec * 1000000000ULL) + (uint64_t)t.tv_nsec;
#endif
}

static void run_case(const char *name, GroupProfile profile) {
    GroupFeatures state;
    GroupFeatures_Init(&state);
    GroupFeatures_Select(&state, profile);
    GroupFeatures_Enable(&state, 1U);
    for (unsigned i=0; i<FRAMES; ++i)
        samples[i] = (int16_t)((i & 1U) ? 1200 : -1200);
    /* Warm up instruction/data caches on host; measure only identical block sizes. */
    for (unsigned j=0; j<2000U; ++j)
        GroupFeatures_Process(&state, samples, output, FRAMES);
    uint64_t start = monotonic_ns();
    for (unsigned j=0; j<BLOCKS; ++j) {
        GroupFeatures_Process(&state, samples, output, FRAMES);
        checksum += (uint16_t)output[j % FRAMES];
    }
    uint64_t stop = monotonic_ns();
    if (start == 0 || stop <= start) return;
    printf("%s,%u,%.3f,%lu\n", name, BLOCKS,
           (double)(stop-start)/(double)BLOCKS,
           (unsigned long)checksum);
}
int main(void) {
    puts("profile,blocks,ns_per_128_frame_block,checksum");
    run_case("neutral_unity", GROUP_PROFILE_NEUTRAL);
    run_case("music_unity", GROUP_PROFILE_MUSIC);
    run_case("voice_attenuation", GROUP_PROFILE_VOICE);
    run_case("wind_highpass", GROUP_PROFILE_WIND);
    return 0;
}
