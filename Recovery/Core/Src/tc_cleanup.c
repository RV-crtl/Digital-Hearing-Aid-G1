/* R27 telecoil-specific conditioning; does not modify Bluetooth DSP.
 * Conservative 180-Hz HP and 4.5-kHz LP at 48 kSa/s; 0.75x headroom,
 * gentle gate only on very quiet intervals and saturating protection.
 * Filters cannot undo clipping or remove noise within the desired speech band. */
#include "tc_cleanup.h"
#include <string.h>
#include <math.h>
void TcCleanup_Reset(TcCleanup *st) { if (st) memset(st,0,sizeof(*st)); }
void TcCleanup_Block(TcCleanup *st,const int16_t *input,int16_t *output,size_t n) {
  if (!st||!input||!output) return;
  /* alpha=exp(-2*pi*180/48000); LP alpha=1-exp(-2*pi*4500/48000) */
  const float hp_alpha=0.023288f, lp_alpha=0.44536f;
  for(size_t i=0;i<n;i++) {
    float x=(float)input[i];
    st->hp_lp += hp_alpha*(x-st->hp_lp);
    float y=x-st->hp_lp;
    st->lp += lp_alpha*(y-st->lp);
    y=st->lp*0.75f;
    /* 2-ms attack / 60-ms release estimate for soft gate.
     * No hard noise threshold for wanted speech above 300 Hz. */
    float mag=fabsf(y);
    float coeff=(mag>st->envelope)?0.01036f:0.000347f;
    st->envelope += coeff*(mag-st->envelope);
    float gate=1.0f;
    if(st->envelope<90.0f) gate=0.32f;
    else if(st->envelope<700.0f) gate=0.32f + 0.68f*(st->envelope-90.0f)/610.0f;
    y *= gate;
    if(y>28500.0f)y=28500.0f;
    if(y< -28500.0f)y= -28500.0f;
    output[i]=(int16_t)y;
  }
}
