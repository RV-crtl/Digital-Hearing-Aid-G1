#include "g1_user_controls.h"
#include <stddef.h>
/* Literal ASCII commands; no heap, locale, strtok or blocking I/O. */
static char upper(char c) {return (c>='a'&&c<='z')?(char)(c-'a'+'A'):c;}
static int equals(const char *a,const char *b)
{
  size_t i=0;
  if (!a||!b)return 0;
  while(a[i] && a[i]!='\r' && a[i]!='\n' && b[i]){
    if(upper(a[i])!=upper(b[i]))return 0;
    ++i;
  }
  return (b[i]=='\0' && (a[i]=='\0'||a[i]=='\r'||a[i]=='\n'));
}
static int prefix(const char *a,const char *p)
{
  size_t i=0;
  while(p[i]) {if(!a[i]||upper(a[i])!=upper(p[i]))return 0;++i;}
  return 1;
}
static uint32_t source(G1UiState *s,uint8_t v)
{
  if(s->source==v)return 0U;
  s->source=v;return G1_UI_SOURCE_CHANGED;
}
static uint32_t mute(G1UiState *s,uint8_t v)
{
  v=(uint8_t)(v!=0U);
  if(s->muted==v)return 0U;
  s->muted=v;return G1_UI_MUTE_CHANGED;
}
static uint32_t gain(G1UiState *s,uint32_t v)
{
  if(v>G1_UI_MAX_GAIN_Q15)v=G1_UI_MAX_GAIN_Q15;
  if(s->gain_q15==v)return 0U;
  s->gain_q15=(uint16_t)v;return G1_UI_GAIN_CHANGED;
}
void G1Ui_Init(G1UiState *s)
{
  if(!s)return;
  *s=(G1UiState){G1_UI_MIC,0U,0U,0U,4096U};
}
const char *G1Ui_SourceName(uint8_t m)
{
  switch(m){case G1_UI_AUTO:return "AUTO";case G1_UI_MIC:return "MIC";
    case G1_UI_BT:return "BT";case G1_UI_TC:return "TC";
    case G1_UI_OFF:return "OFF";default:return "INVALID";}
}
uint32_t G1Ui_Percent(const G1UiState *s)
{
  return s?(uint32_t)(((uint32_t)s->gain_q15*100U+G1_UI_MAX_GAIN_Q15/2U)/G1_UI_MAX_GAIN_Q15):0U;
}
uint32_t G1Ui_Encoder(G1UiState *s,int detents)
{
  if(!s||detents==0)return 0U;
  if(detents>128)detents=128;
  if(detents< -128)detents=-128;
  int32_t v=(int32_t)s->gain_q15+(int32_t)detents*(int32_t)G1_UI_STEP_Q15;
  if(v<0)v=0;
  return gain(s,(uint32_t)v);
}
uint32_t G1Ui_Touch(G1UiState *s,uint16_t e)
{
  if(!s)return G1_UI_INVALID;
  /* Anna's wired controls: E8 BT, E9 TC, E10 mute, E11 MIC.
   * E0..E7 inactive. Proximity bit 12 never selects a source. */
  uint32_t flags=0U;
  if(e&(1U<<8))flags|=source(s,G1_UI_BT);
  if(e&(1U<<9))flags|=source(s,G1_UI_TC);
  if(e&(1U<<10))flags|=mute(s,(uint8_t)!s->muted);
  if(e&(1U<<11))flags|=source(s,G1_UI_MIC);
  return flags;
}
static int parse_percent(const char *arg,uint32_t *v)
{
  uint32_t n=0U;
  if(!arg || !*arg)return 0;
  for(;*arg && *arg!='\r' && *arg!='\n';arg++){
    if(*arg<'0'||*arg>'9')return 0;
    n=n*10U+(uint32_t)(*arg-'0');
    if(n>100U)return 0;
  }
  /* R20 regression fix: serial terminals commonly append CR, LF or CRLF. */
  while(*arg=='\r'||*arg=='\n')++arg;
  if(*arg)return 0;
  *v=n;return 1;
}
uint32_t G1Ui_Command(G1UiState *s,const char *line)
{
  if(!s||!line)return G1_UI_INVALID;
  while(*line==' '||*line=='\t')++line;
  if(equals(line,"SOURCE AUTO")||equals(line,"MODE AUTO"))return source(s,G1_UI_AUTO);
  if(equals(line,"SOURCE MIC")||equals(line,"MODE MIC"))return source(s,G1_UI_MIC);
  if(equals(line,"SOURCE BT")||equals(line,"MODE BT"))return source(s,G1_UI_BT);
  if(equals(line,"SOURCE TC")||equals(line,"MODE TC"))return source(s,G1_UI_TC);
  if(equals(line,"SOURCE OFF")||equals(line,"MODE OFF"))return source(s,G1_UI_OFF);
  if(equals(line,"MUTE ON")||equals(line,"OUTPUT OFF")||equals(line,"MUTE"))return mute(s,1U);
  if(equals(line,"MUTE OFF")||equals(line,"OUTPUT ON")||equals(line,"UNMUTE"))return mute(s,0U);
  if(equals(line,"MUTE TOGGLE"))return mute(s,(uint8_t)!s->muted);
  if(equals(line,"VOL +")||equals(line,"VOLUME +"))return G1Ui_Encoder(s,1);
  if(equals(line,"VOL -")||equals(line,"VOLUME -"))return G1Ui_Encoder(s,-1);
  if(equals(line,"OUTPUT LEVEL SAFE"))return gain(s,4096U);
  if(equals(line,"OUTPUT LEVEL LOW"))return gain(s,8192U);
  if(equals(line,"OUTPUT LEVEL MED")||equals(line,"OUTPUT LEVEL HIGH")||equals(line,"OUTPUT LEVEL UNITY"))return gain(s,G1_UI_MAX_GAIN_Q15);
  if(equals(line,"DSP ON")){if(s->dsp)return 0U;s->dsp=1U;return G1_UI_DSP_CHANGED;}
  if(equals(line,"DSP OFF")){if(!s->dsp)return 0U;s->dsp=0U;return G1_UI_DSP_CHANGED;}
  if(equals(line,"ADAPT ON")){if(s->adaptive)return 0U;s->adaptive=1U;return G1_UI_PREF_CHANGED;}
  if(equals(line,"ADAPT OFF")){if(!s->adaptive)return 0U;s->adaptive=0U;return G1_UI_PREF_CHANGED;}
  if(prefix(line,"VOLUME ")||prefix(line,"VOL ")){
    const char *v=line+(prefix(line,"VOLUME ")?7U:4U);
    uint32_t pct;
    if(!parse_percent(v,&pct))return G1_UI_INVALID;
    return gain(s,(uint32_t)((pct*G1_UI_MAX_GAIN_Q15+50U)/100U));
  }
  if(prefix(line,"SOURCE ")||prefix(line,"MODE ")||prefix(line,"MUTE ")||prefix(line,"ADAPT ")||prefix(line,"DSP "))return G1_UI_INVALID;
  return G1_UI_UNKNOWN;
}
