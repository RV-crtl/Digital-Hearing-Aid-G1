#include "group_encoder.h"
void GroupEncoder_Init(GroupEncoder *e, uint8_t a, uint8_t b)
{
  if (!e) return;
  *e=(GroupEncoder){0};
  e->previous=(uint8_t)(((a&1U)<<1U)|(b&1U));
}
int GroupEncoder_Update(GroupEncoder *e, uint8_t a, uint8_t b)
{
  if (!e) return 0;
  uint8_t now=(uint8_t)(((a&1U)<<1U)|(b&1U));
  if (now==e->previous) return 0;
  /* 00->01->11->10->00 clockwise; opposite negative. */
  static const int8_t transitions[16]={0,1,-1,0,-1,0,0,1,1,0,0,-1,0,-1,1,0};
  int step=transitions[(e->previous<<2U)|now];
  if (step==0) {e->quarter_steps=0;e->bad_edges++;}
  else {
    e->quarter_steps=(int8_t)(e->quarter_steps+step);
    if (e->quarter_steps>=4) {e->quarter_steps=0;e->previous=now;return 1;}
    if (e->quarter_steps<=-4) {e->quarter_steps=0;e->previous=now;return -1;}
  }
  e->previous=now;
  return 0;
}
