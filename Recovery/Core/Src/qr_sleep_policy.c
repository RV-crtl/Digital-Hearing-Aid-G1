#include "qr_sleep_policy.h"
void QrSleep_Init(QrSleepPolicy *s){if(s){s->asleep=0U;s->armed=0U;s->pending=0U;s->high_since=0U;}}
void QrSleep_Enter(QrSleepPolicy *s){if(s){s->asleep=1U;s->armed=0U;s->pending=0U;s->high_since=0U;}}
uint8_t QrSleep_Update(QrSleepPolicy *s,uint8_t present,uint8_t proximity,uint32_t now){
 if(!s||!s->asleep)return 0U;
 if(!present){s->pending=0U;return 0U;}
 if(!proximity){s->armed=1U;s->pending=0U;return 0U;}
 if(!s->armed)return 0U;
 if(!s->pending){s->pending=1U;s->high_since=now;return 0U;}
 if((uint32_t)(now-s->high_since)<120U)return 0U;
 s->asleep=0U;s->pending=s->armed=0U;return 1U;
}
