#include "group_encoder.h"
#include <assert.h>
#include <stdio.h>
int main(void){GroupEncoder g;GroupEncoder_Init(&g,0,0);g.transitions_per_step=2;assert(GroupEncoder_Update(&g,0,1)==0);assert(GroupEncoder_Update(&g,1,1)==1);assert(GroupEncoder_Update(&g,0,1)==0);assert(GroupEncoder_Update(&g,0,0)==-1);assert(g.bad_edges==0);puts("R12_ENCODER_TWO_TRANSITION_TEST_PASS");return 0;}
