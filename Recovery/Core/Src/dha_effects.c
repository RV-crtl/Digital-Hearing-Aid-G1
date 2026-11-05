#include "dha_effects.h"
#include <string.h>
#define DHA_ROOM_DELAY 512U
static int16_t history[DHA_ROOM_DELAY];
static uint16_t history_pos;
static volatile uint8_t room_enabled;
static volatile uint8_t noise_enabled;
static int32_t nr_y, nr_last_x;
static int16_t clip(int32_t x) {return (int16_t)(x>30000?30000:x< -30000?-30000:x);}
void DhaEffects_Init(void){memset(history,0,sizeof(history));history_pos=0;nr_y=0;nr_last_x=0;room_enabled=0;noise_enabled=0;}
void DhaEffects_SetRoom(uint8_t e){__atomic_store_n(&room_enabled,(uint8_t)!!e,__ATOMIC_RELAXED);}
void DhaEffects_SetNoiseReduction(uint8_t e){__atomic_store_n(&noise_enabled,(uint8_t)!!e,__ATOMIC_RELAXED);}
void DhaEffects_Process(int16_t *a,size_t n,uint8_t source){
 if(!a)return;
 uint8_t room=__atomic_load_n(&room_enabled,__ATOMIC_RELAXED);
 uint8_t nr=__atomic_load_n(&noise_enabled,__ATOMIC_RELAXED);
 for(size_t i=0;i<n;i++){
   int32_t y=a[i];
   /* MIC = 3; 1-pole ~150 Hz HPF at 48 kHz. No adaptive threshold/muting. */
   if(source==3U){
     int32_t d=y-nr_last_x;nr_last_x=y;
     nr_y=(int32_t)(((int64_t)nr_y*32150)/32768)+d;
     if(nr) y=clip(nr_y);
   }
   /* BT = 2, TC = 1. Subtle short reflection using a mono transducer. Spatial
    * processing is deliberately disabled for TELECOIL and microphone. */
   if(source==2U && room){
     int16_t delay=history[history_pos];
     history[history_pos]=(int16_t)y;
     history_pos=(uint16_t)((history_pos+1U)%DHA_ROOM_DELAY);
     y=clip((y*7 + (int32_t)delay)/8);
   }
   a[i]=(int16_t)y;
 }
}
