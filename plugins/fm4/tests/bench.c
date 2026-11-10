// Native regression timing only; no Pi performance claim. SPDX-License-Identifier: MIT
#include "fm4_dsp.h"
#include <stdio.h>
#include <string.h>
#include <time.h>
static double ns(void) { struct timespec t; clock_gettime(CLOCK_MONOTONIC_RAW,&t);return t.tv_sec*1e9+t.tv_nsec; }
int main(void) {
    puts("algorithm,scenario,instances,mean_ns_per_12_frame_group,max_observed_ns_per_group");
    volatile float sink=0;
    for(int algorithm=0;algorithm<8;++algorithm) for(int scenario=0;scenario<3;++scenario) for(int count=1;count<=4;count*=2) {
        fm4_state s[4]; float p[16],out[12]; memcpy(p,fm4_defaults,sizeof p);
        p[ALGO]=algorithm*16;p[GATE]=127;
        if(scenario) { p[FM]=p[FDBK]=p[DRFT]=p[RAT]=127;p[BODY]=0; }
        for(int j=0;j<count;++j) {fm4_init(&s[j],48000,42+j);fm4_params(&s[j],p);fm4_note_on(&s[j],scenario?115+j:48+j,1);}
        double total=0,max=0;
        for(int k=0;k<11000;++k) {
            if(scenario==2 && k%10==0) {p[ALGO]=((algorithm+k/10)%8)*16;p[MRPH]=(k/10)%128;for(int j=0;j<count;++j) fm4_params(&s[j],p);}
            double a=ns();for(int j=0;j<count;++j) {fm4_render(&s[j],out,12);sink+=out[0];}double elapsed=ns()-a;
            if(k>=1000) {total+=elapsed;if(elapsed>max)max=elapsed;}
        }
        printf("%d,%s,%d,%.1f,%.1f\n",algorithm,scenario==0?"default":scenario==1?"extreme":"changes",count,total/10000,max);
    }
    return sink==123456789;
}
