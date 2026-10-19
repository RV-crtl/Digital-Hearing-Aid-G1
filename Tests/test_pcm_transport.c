/* Transport and fixed-point boundary regression. No MCU peripherals required. */
#include "pcm_transport.h"
#include "audio_processing.h"
#include <assert.h>
#include <limits.h>
#include <stdint.h>
#include <stdio.h>

int main(void) {
    assert(Audio_IsSupportedSampleRate(16000U));
    assert(Audio_IsSupportedSampleRate(48000U));
    assert(!Audio_IsSupportedSampleRate(44100U));
    assert(PcmTransport_S24WordToS16(0x007FFFFFU)==INT16_MAX);
    assert(PcmTransport_S24WordToS16(0x00800000U)==INT16_MIN);
    assert(PcmTransport_S24WordToS16(0x00FFFFFFU)==-1);
    assert(PcmTransport_S24WordToS16(0x00000000U)==0);
    assert(PcmTransport_DecodeWordMode(0x12345678U,PCM_DECODE_UPPER16)==0x1234);
    assert(PcmTransport_DecodeWordMode(0x12345678U,PCM_DECODE_MIDDLE16)==0x3456);
    assert(PcmTransport_DecodeWordMode(0x12345678U,PCM_DECODE_LOW16)==0x5678);
    assert(PcmTransport_DecodeWordMode(0x12345678U,PCM_DECODE_LOW24)==0x3456);

    const uint8_t packed[9]={0,0,0,0xFF,0xFF,0x7F,0,0,0x80};
    int16_t converted[3]={1,1,1};
    assert(PcmTransport_DecodePackedS24Le(packed,3U,converted)==PCM_TRANSPORT_OK);
    assert(converted[0]==0 && converted[1]==INT16_MAX && converted[2]==INT16_MIN);
    assert(PcmTransport_DecodePackedS24Le(NULL,0U,NULL)==PCM_TRANSPORT_OK);
    assert(PcmTransport_DecodePackedS24Le(NULL,1U,converted)==PCM_TRANSPORT_ERR_ARGUMENT);

    const uint32_t frames[4]={0x7FFFFFU,0x800000U,0x000100U,0xFFFFFFU};
    int16_t l[2]={0,0},r[2]={0,0},mono[2]={42,42},stereo[4]={0};
    assert(PcmTransport_DecodeStereoWords(frames,2U,l,r)==PCM_TRANSPORT_OK);
    assert(l[0]==INT16_MAX && r[0]==INT16_MIN);
    assert(l[1]==1 && r[1]==-1);
    assert(PcmTransport_DecodeStereoWordsToMono(frames,2U,mono)==PCM_TRANSPORT_OK);
    assert(mono[0]==0 && mono[1]==0);
    assert(PcmTransport_MonoToStereoS16(mono,2U,stereo)==PCM_TRANSPORT_OK);
    for (size_t i=0U;i<4U;++i) assert(stereo[i]==0);
    assert(PcmTransport_DecodeStereoWords(NULL,0U,NULL,NULL)==PCM_TRANSPORT_OK);
    assert(PcmTransport_DecodeStereoWords(NULL,1U,l,r)==PCM_TRANSPORT_ERR_ARGUMENT);
    assert(PcmTransport_MonoToStereoS16(NULL,1U,stereo)==PCM_TRANSPORT_ERR_ARGUMENT);

    assert(Audio_ApplyGainQ15(INT16_MIN,32768)==INT16_MIN);
    assert(Audio_ApplyGainQ15(INT16_MAX,32768)==INT16_MAX);
    assert(Audio_ApplyGainQ15(INT16_MAX,65536)==INT16_MAX);
    assert(Audio_ApplyGainQ15(INT16_MIN,65536)==INT16_MIN);
    assert(Audio_ApplyGainQ15(123,-1)==0);
    assert(Audio_MixStereoToMono(INT16_MAX,INT16_MIN)==0);
    const int16_t raw[4]={100,-100,INT16_MAX,INT16_MAX};
    int16_t out[2]={77,77};
    assert(Audio_ProcessStereoToMono(raw,out,2U,32768,false));
    assert(out[0]==0 && out[1]==INT16_MAX);
    assert(Audio_ProcessStereoToMono(raw,out,2U,32768,true));
    assert(out[0]==0 && out[1]==0);
    out[0]=77;
    assert(!Audio_ProcessStereoToMono(raw,out,2U,-1,false));
    assert(out[0]==77);
    assert(Audio_ProcessStereoToMono(NULL,NULL,0U,32768,false));
    assert(!Audio_ProcessStereoToMono(NULL,out,1U,32768,false));
    puts("PCM/GAIN: S24 signs, decode modes, stereo boundaries, mute, saturation PASS");
    return 0;
}
