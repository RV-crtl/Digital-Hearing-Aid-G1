/* Host-side C reference vectors from the SAME production audio_dynamics.c.
 * Not a substitute for SAI/DMA hardware-in-the-loop measurements.
 */
#include "audio_dynamics.h"
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#define N 4096
#define B 128
int main(int argc, char **argv) {
    FILE *fp = stdout;
    if(argc > 2) return 2;
    if(argc == 2) {fp=fopen(argv[1],"w"); if(!fp) return 3;}
    fprintf(fp,"configuration,sample,input,output\n");
    for(int enabled=0;enabled<2;++enabled){
        AudioDynamics_t state;
        AudioDynamicsConfig_t cfg=AudioDynamics_DefaultConfig();
        cfg.compressor_default=false;
        cfg.resonance_eq_default=(enabled!=0);
        if(AudioDynamics_Init(&state,&cfg)!=AUDIO_DYNAMICS_OK) return 4;
        int16_t in[B],out[B];
        for(unsigned offset=0;offset<N;offset+=B){
            memset(in,0,sizeof(in));
            if(offset==0) in[0]=10000;
            if(AudioDynamics_ProcessBlock(&state,in,out,B,false)!=AUDIO_DYNAMICS_OK)return 5;
            for(unsigned j=0;j<B;++j)
                fprintf(fp,"%s,%u,%d,%d\n",enabled?"hpf_eq":"hpf",offset+j,(int)in[j],(int)out[j]);
        }
    }
    if(fp!=stdout && fclose(fp)!=0) return 6;
    return 0;
}
