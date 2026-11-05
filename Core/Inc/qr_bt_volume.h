#ifndef QR_BT_VOLUME_H
#define QR_BT_VOLUME_H
#include <stdint.h>
#include <stddef.h>
/* User-requested CA100 is not confirmed by the published BT401 0..30 manual.
 * Keep in one place so a confirmed module variant can be selected without
 * touching the task/state-machine. It is NOT a phone-volume API. */
#define QR_BT_CONNECT_VOLUME_COMMAND "AT+CA100\r\n"
#define QR_BT_VOLUME_QUERY_COMMAND   "AT+QA\r\n"
typedef struct { uint8_t previous_link; uint32_t connection_edges; } QrBtVolume;
void QrBtVolume_Init(QrBtVolume *s);
uint8_t QrBtVolume_ConnectedEdge(QrBtVolume *s,uint8_t linked);
#endif
