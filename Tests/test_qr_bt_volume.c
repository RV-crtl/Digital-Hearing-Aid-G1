#include "qr_bt_volume.h"
#include <assert.h>
#include <string.h>
#include <stdio.h>
int main(void){
 QrBtVolume s;QrBtVolume_Init(&s);
 assert(!QrBtVolume_ConnectedEdge(&s,0));
 assert(QrBtVolume_ConnectedEdge(&s,1));
 assert(!QrBtVolume_ConnectedEdge(&s,1));
 assert(!QrBtVolume_ConnectedEdge(&s,0));
 assert(QrBtVolume_ConnectedEdge(&s,1));
 assert(s.connection_edges==2);
 assert(!strcmp(QR_BT_CONNECT_VOLUME_COMMAND,"AT+CA100\r\n"));
 puts("PASS BT edge rearmed on disconnect and CA100 requested each connect");
 return 0;
}
