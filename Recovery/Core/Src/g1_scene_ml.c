#include "g1_scene_ml.h"
#include <stddef.h>
#include <limits.h>

static uint32_t distance3(const uint16_t *c,const uint8_t *x){
  uint32_t d=0U;
  for(unsigned k=0;k<3U;k++){
    int32_t v=(int32_t)c[k]-(int32_t)x[k];
    d+=(uint32_t)(v*v);
  }
  return d;
}
void G1Scene_Init(G1SceneLearner *m){
  if(!m)return;
  *m=(G1SceneLearner){0};
  /* Acoustic prototype seeds (not labelled training examples). */
  const uint16_t seed[3][3]={{65U,35U,55U},{145U,72U,62U},{215U,100U,115U}};
  for(unsigned i=0;i<3U;i++)for(unsigned k=0;k<3U;k++)m->centre[i][k]=seed[i][k];
}
uint8_t G1Scene_Observe(G1SceneLearner *m,uint8_t energy,uint8_t zcr,uint8_t crest){
  if(!m)return 0U;
  const uint8_t x[3]={energy,zcr,crest};
  uint32_t first=UINT32_MAX,second=UINT32_MAX;
  uint8_t best=0U;
  for(uint8_t i=0;i<3U;i++){
    uint32_t d=distance3(m->centre[i],x);
    if(d<first){second=first;first=d;best=i;}
    else if(d<second)second=d;
  }
  m->last_cluster=best;
  /* Confidence is a relative distance-margin indicator, never an ML accuracy. */
  m->confidence_pct=(uint8_t)((second>first)?
    (((uint32_t)(second-first)*100U)/(uint32_t)(second+1U)):0U);
  if(m->observations[best]<UINT16_MAX)m->observations[best]++;
  ++m->trained_blocks;
  /* A bounded 1/32 exponential centroid update on every 8th block.
   * Rounding toward input prevents quantisation freezing for small deltas. */
  if((m->trained_blocks&7U)==0U){
    for(unsigned k=0U;k<3U;k++){
      int32_t diff=(int32_t)x[k]-(int32_t)m->centre[best][k];
      int32_t step=diff/32;
      if(step==0 && diff!=0)step=(diff>0)?1:-1;
      m->centre[best][k]=(uint16_t)((int32_t)m->centre[best][k]+step);
    }
  }
  return best;
}
