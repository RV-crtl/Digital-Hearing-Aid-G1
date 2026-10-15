#include "group_encoder.h"
#include <assert.h>
#include <stdio.h>
int main(void){
 GroupEncoder e;GroupEncoder_Init(&e,0,0);
 int sum=0;sum+=GroupEncoder_Update(&e,0,1);sum+=GroupEncoder_Update(&e,1,1);sum+=GroupEncoder_Update(&e,1,0);sum+=GroupEncoder_Update(&e,0,0);
 assert(sum==1);
 sum=0;sum+=GroupEncoder_Update(&e,1,0);sum+=GroupEncoder_Update(&e,1,1);sum+=GroupEncoder_Update(&e,0,1);sum+=GroupEncoder_Update(&e,0,0);
 assert(sum==-1);
 GroupEncoder_Update(&e,1,1);assert(e.bad_edges==1);
 puts("Quadrature encoder: clockwise/counterclockwise/glitch PASS");return 0;
}
