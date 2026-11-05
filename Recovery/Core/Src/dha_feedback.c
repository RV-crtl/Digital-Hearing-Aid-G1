#include "dha_feedback.h"
/* Original percussive feedback with a deliberately audible mid-frequency
 * attack for small transducers. No assumption about measured acoustic SPL. */
static const int16_t sine64[64] = {0,3212,6393,9512,12539,15446,18204,20787,23170,25329,27245,28898,30273,31356,32137,32609,32767,32609,32137,31356,30273,28898,27245,25329,23170,20787,18204,15446,12539,9512,6393,3212,0,-3212,-6393,-9512,-12539,-15446,-18204,-20787,-23170,-25329,-27245,-28898,-30273,-31356,-32137,-32609,-32767,-32609,-32137,-31356,-30273,-28898,-27245,-25329,-23170,-20787,-18204,-15446,-12539,-9512,-6393,-3212};
static volatile uint32_t pending;
static uint32_t phase;
static uint32_t transient_phase;
static uint32_t remaining;
static uint32_t duration;
static uint32_t increment;
static uint32_t noise_lfsr=0xA531C9U;
static uint8_t repeats;
static uint32_t pause_count;
static uint8_t event_id;
static int16_t sat16(int32_t x) { return (int16_t)(x>32767?32767:x< -32768?-32768:x); }
void DhaFeedback_Request(DhaCue cue) {
 if((unsigned)cue<(unsigned)DHA_CUE_COUNT) (void)__atomic_fetch_or(&pending,(uint32_t)1UL<<(unsigned)cue,__ATOMIC_RELAXED);
}
uint32_t DhaFeedback_Pending(void) {return __atomic_load_n(&pending,__ATOMIC_RELAXED);}
void DhaFeedback_ClearPending(void){__atomic_store_n(&pending,0U,__ATOMIC_RELEASE);}
static void begin_event(unsigned id) {
 event_id=(uint8_t)id; phase=0; transient_phase=0; remaining=0;pause_count=0;
 repeats=(id==DHA_CUE_BOOT || id==DHA_CUE_BT_CONNECTED || id==DHA_CUE_BT_DISCONNECTED)?2U:1U;
 duration=(id==DHA_CUE_BOOT||id==DHA_CUE_BT_CONNECTED||id==DHA_CUE_BT_DISCONNECTED)?4000U:3300U;
 /* Original kick-like frequency sweep, not a transcription of any sound. */
 increment=(id==DHA_CUE_BT_CONNECTED ? 13000000U :
            id==DHA_CUE_TC ? 10200000U :
            id==DHA_CUE_MIC ? 12000000U :
            id==DHA_CUE_BT_DISCONNECTED ? 7600000U :
            id==DHA_CUE_MUTE ? 8000000U : 11000000U);
}
void DhaFeedback_Render(int16_t *stereo,size_t frames) {
 if(!stereo)return;
 if(!remaining && !repeats) {
   uint32_t mask=__atomic_exchange_n(&pending,0U,__ATOMIC_ACQ_REL);
   if(mask) {
     unsigned id=0U;
     while(id<(unsigned)DHA_CUE_COUNT && !(mask&(1UL<<id)))id++;
     if(id<(unsigned)DHA_CUE_COUNT)begin_event(id);
     /* Preserve other notifications rather than silently dropping them. */
     mask &= ~(1UL<<id);
     if(mask)(void)__atomic_fetch_or(&pending,mask,__ATOMIC_RELAXED);
   }
 }
 for(size_t i=0;i<frames;i++) {
   if(!remaining && repeats){
     if(pause_count)pause_count--;
     else {remaining=duration;repeats--;phase=0;}
   }
   int32_t kick=0;
   if(remaining){
     uint32_t elapsed=duration-remaining;
     uint32_t amp=(8500U*remaining)/duration;
     /* 60ms-ish drum body, brief initial click; no continuous oscillator. */
     uint32_t step=increment-(uint32_t)(((uint64_t)increment*elapsed)/(duration*2U));
     phase+=step;
     kick=((int32_t)sine64[(phase>>26)&63U]*(int32_t)amp)/32768;
     /* Brief pitched upper harmonic gives a small transducer an audible
      * attack even if its physical response falls off near 100 Hz. */
     if(elapsed<720U){
       transient_phase+=90000000UL; /* ~1 kHz attack, audible on small speakers */
       int32_t upper=(int32_t)(720U-elapsed)*4800/720;
       kick+=((int32_t)sine64[(transient_phase>>26)&63U]*upper)/32768;
     }
     if(elapsed<80U){
       noise_lfsr^=noise_lfsr<<13;noise_lfsr^=noise_lfsr>>17;noise_lfsr^=noise_lfsr<<5;
       kick+=((noise_lfsr&1U)?1:-1)*(int32_t)((80U-elapsed)*9U);
     }
     remaining--;
     if(!remaining && repeats)pause_count=1000U;
   }
   stereo[2*i]=sat16((int32_t)stereo[2*i]+kick);
   stereo[2*i+1]=sat16((int32_t)stereo[2*i+1]+kick);
 }
 (void)event_id;
}
