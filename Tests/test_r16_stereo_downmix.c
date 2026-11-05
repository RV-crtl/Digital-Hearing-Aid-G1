#include "audio_processing.h"
#include "pcm_transport.h"
#include <assert.h>
#include <stdio.h>
int main(void) {
 int16_t out[4]={0};
 const uint32_t pcm_left_only[8]={0x0003E800U,0U,0x0003E800U,0U,0x0003E800U,0U,0x0003E800U,0U};
 const uint32_t pcm_right_only[8]={0U,0x0003E800U,0U,0x0003E800U,0U,0x0003E800U,0U,0x0003E800U};
 const uint32_t pcm_both[8]={0x0003E800U,0x0003E800U,0x0003E800U,0x0003E800U,0x0003E800U,0x0003E800U,0x0003E800U,0x0003E800U};
 assert(PcmTransport_DecodeStereoWordsToMono(pcm_left_only,4U,out)==PCM_TRANSPORT_OK);
 for(int i=0;i<4;i++)assert(out[i]==500);
 assert(PcmTransport_DecodeStereoWordsToMono(pcm_right_only,4U,out)==PCM_TRANSPORT_OK);
 for(int i=0;i<4;i++)assert(out[i]==500);
 assert(PcmTransport_DecodeStereoWordsToMono(pcm_both,4U,out)==PCM_TRANSPORT_OK);
 for(int i=0;i<4;i++)assert(out[i]==1000);
 puts("R16_BT_STEREO_DOWNMIX_LEFT_RIGHT_BOTH_PASS");return 0;
}
