#include "qr_bt_volume.h"
void QrBtVolume_Init(QrBtVolume *s){if(s){s->previous_link=0U;s->connection_edges=0U;}}
uint8_t QrBtVolume_ConnectedEdge(QrBtVolume *s,uint8_t linked){
 if(!s)return 0U;
 linked=(uint8_t)(linked!=0U);
 uint8_t edge=(uint8_t)(linked && !s->previous_link);
 s->previous_link=linked;
 if(edge)++s->connection_edges;
 return edge;
}
