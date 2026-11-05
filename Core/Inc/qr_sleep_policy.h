#ifndef QR_SLEEP_POLICY_H
#define QR_SLEEP_POLICY_H
#include <stdint.h>
/* Logical audio/RF sleep: control task and I2C polling stay alive so MPR121
 * proximity can wake. Not MCU STOP mode; PCB lacks BT power-switch feedback. */
typedef struct { uint8_t asleep, armed, pending; uint32_t high_since; } QrSleepPolicy;
void QrSleep_Init(QrSleepPolicy *s);
void QrSleep_Enter(QrSleepPolicy *s);
/* Require clear proximity after entry, then sustained proximity >= 120 ms. */
uint8_t QrSleep_Update(QrSleepPolicy *s,uint8_t touch_present,uint8_t proximity,uint32_t now_ms);
#endif
