#include "qr_demo_test.h"
#include "qr_sleep_policy.h"
#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>
#include <assert.h>
#define N 128
int main(void){
  QrDemoTest t;QrDemoTest_Init(&t);
  int16_t block[N*2];
  int32_t peak=0;unsigned signs=0;int last=0;
  for(int k=0;k<180;k++){
    QrDemoTest_Render(&t,1,block,N);
    for(int i=0;i<N;i++){
      int x=block[2*i];
      if(abs(x)>peak)peak=abs(x);
      assert(block[2*i]==block[2*i+1]);
      if(last<0 && x>=0)signs++;
      last=x;
    }
  }
  assert(peak<=QR_TEST_PEAK_PCM && peak>30000);
  /* A 1 kHz generator at 48 kHz should have about 480 periods / 0.48s. */
  assert(signs>=470 && signs<=490);
  /* Chirp bounded and changing with time. */
  QrDemoTest_Render(&t,4,block,N);
  unsigned diff=0;
  for(unsigned i=1;i<N;i++){
    assert(abs(block[2*i])<=QR_TEST_PEAK_PCM);
    if(block[2*i]!=block[2*(i-1)])diff++;
  }
  assert(diff>80);
  QrDemoTest_Render(&t,0,block,N);
  assert(QrDemoTest_IsActive(&t)); /* 17 ms ramp-out prevents hard stop click */
  for(int rep=0;rep<10;rep++)QrDemoTest_Render(&t,0,block,N);
  assert(!QrDemoTest_IsActive(&t));
  for(int i=0;i<N*2;i++)assert(block[i]==0);
  QrSleepPolicy sl;QrSleep_Init(&sl);QrSleep_Enter(&sl);
  assert(sl.asleep);
  assert(!QrSleep_Update(&sl,1,1,100)); /* old proximity held high */
  assert(!QrSleep_Update(&sl,1,0,150)); /* release arms */
  assert(!QrSleep_Update(&sl,1,1,160));
  assert(!QrSleep_Update(&sl,1,1,250));
  assert(!QrSleep_Update(&sl,0,0,251)); /* missing MPR121 resets debounce */
  assert(!QrSleep_Update(&sl,1,1,300));
  assert(!QrSleep_Update(&sl,1,1,410));
  assert(QrSleep_Update(&sl,1,1,421));
  assert(!sl.asleep);
  puts("QR: tone/chirp PCM limits and sleep proximity policy PASS");
  return 0;
}
