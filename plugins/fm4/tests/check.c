// Independent native assertions. SPDX-License-Identifier: MIT
#include "fm4_dsp.h"
#include "rmp/rmp_api.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
extern const rmp_plugin rmp_entry;
#define CHECK(c) do { if(!(c)) { fprintf(stderr,"FAIL %s:%d: %s\n",__FILE__,__LINE__,#c); exit(1); } } while(0)
static void block(fm4_state *s,int n) { float x[128]; CHECK(n<=128); fm4_render(s,x,n); for(int i=0;i<n;++i) CHECK(isfinite(x[i]) && fabsf(x[i])<=.950001f); }
static void frames(fm4_state *s,int n) { while(n>0) { int k=n>12?12:n; block(s,k); n-=k; } }
static void clean(float *p) { memcpy(p,fm4_defaults,64); p[FM]=p[FDBK]=p[DRFT]=p[SWP]=p[MRPH]=0; p[GATE]=127; p[BODY]=127; p[RAT]=0; }
static float ref_sine(float phase,float pm,float gain) { return gain*sinf(phase+pm); }
static void routing(void) {
    const unsigned carriers[8]={1,5,9,1,7,5,1,15};
    for(int a=0;a<8;++a) {
        fm4_state s; CHECK(!fm4_init(&s,48000,1));
        float p[16]; memcpy(p,fm4_defaults,sizeof p); p[ALGO]=a*16;
        p[COL]=64; p[MRPH]=0; p[BODY]=0; p[FDBK]=40;
        fm4_params(&s,p); fm4_note_on(&s,60,1);
        fm4_bank b=s.bank[0];
        CHECK(fm4_routes[a].carriers==carriers[a]);
        float phase[4]={.1f,.2f,.3f,.4f},old[4]={.7f,-.6f,.5f,-.4f},inc[4]={.017f,.023f,.031f,.047f};
        memcpy(b.phase,phase,sizeof phase); memcpy(b.previous,old,sizeof old);
        float f=2.5f*(40.0f/127)*(40.0f/127),idx=.5f,activity=.7f;
        for(int t=0;t<300;++t) {
            float y[4],g[4]; for(int i=0;i<4;++i) g[i]=(carriers[a]&(1u<<i))?1:.35f+.65f*activity;
            // Independent explicit equations: feedback always from the saved old vector.
            float pd=0,pc=0,pb=0,pa=0;
            switch(a) {
            case 0: case 1: case 3: case 4: pd=f*old[3]; break;
            case 5: case 6: pd=f*old[0]; break;
            case 7: pd=f*old[3]; break;
            }
            y[3]=ref_sine(phase[3],pd,g[3]);
            switch(a) {
            case 0: case 1: case 5: case 6: pc=idx*y[3]; break;
            case 2: case 3: case 7: pc=f*old[2]; break;
            case 4: pc=idx*y[3]; break;
            }
            y[2]=ref_sine(phase[2],pc,g[2]);
            switch(a) {
            case 0: case 2: case 6: pb=idx*y[2]; break;
            case 1: case 3: case 7: pb=f*old[1]; break;
            case 4: pb=idx*y[3]; break;
            case 5: pb=f*old[2]; break;
            }
            y[1]=ref_sine(phase[1],pb,g[1]);
            switch(a) {
            case 0: case 1: case 2: case 5: case 6: pa=idx*y[1]; break;
            case 3: pa=idx*(y[1]+y[2]+y[3])/3; break;
            case 7: pa=f*old[0]; break;
            }
            y[0]=ref_sine(phase[0],pa,g[0]);
            float expected=0; int count=0;
            for(int i=0;i<4;++i) if(carriers[a]&(1u<<i)) { expected+=y[i]; ++count; }
            float actual=fm4_bank_tick(&b,inc,idx,activity);
            CHECK(fabsf(actual-expected/count)<1e-5f);
            for(int i=0;i<4;++i) { CHECK(fabsf(b.previous[i]-y[i])<1e-5f); phase[i]+=inc[i]; if(phase[i]>=6.28318530718f) phase[i]-=6.28318530718f; }
            memcpy(old,y,sizeof old);
        }
    }
    puts("routing: eight explicit equation references PASS");
}
static void lifecycle(void) {
    float p[16]; clean(p); fm4_state s;
    for(int rate=44100;rate<=96000;rate+=(rate==44100?3900:48000)) {
        CHECK(!fm4_init(&s,(float)rate,42));
        float x[19]; fm4_render(&s,x,19); for(int i=0;i<19;++i) CHECK(x[i]==0);
        p[ATK]=0; p[REL]=0; p[DEC]=0; p[GATE]=127;
        fm4_params(&s,p); fm4_note_on(&s,69,1);
        frames(&s,(int)(rate*.0005f)); CHECK(s.amplitude>.4f && s.amplitude<.6f);
        frames(&s,rate/20); CHECK(s.stage==2 && s.amplitude==1);
        fm4_note_off(&s); frames(&s,rate/50); CHECK(s.stage==0);
        p[GATE]=0; fm4_params(&s,p); fm4_note_on(&s,69,1); fm4_note_off(&s);
        frames(&s,(int)(rate*.015f)); CHECK(s.stage==3);
        frames(&s,(int)(rate*.018f)); CHECK(s.stage==0); // 1+20+10 ms
        p[GATE]=127; fm4_params(&s,p); fm4_note_on(&s,48,1); frames(&s,rate/20);
        p[GATE]=0; fm4_params(&s,p); CHECK(s.gated==1); // mode latched
        fm4_note_on(&s,72,.7f); CHECK(s.retrigger>0); fm4_note_on(&s,76,.5f);
        frames(&s,rate/500+2); CHECK(s.note==76 && s.gated==0 && s.note_count==4);
        fm4_leave(&s); p[GATE]=127; fm4_params(&s,p); fm4_note_on(&s,60,1); frames(&s,rate/20);
        fm4_note_on(&s,61,1); fm4_note_off(&s); frames(&s,rate/20); CHECK(!s.stage);
        fm4_note_on(&s,60,0); frames(&s,100); CHECK(!s.stage);
        for(int i=0;i<100;++i) { fm4_note_on(&s,(float)(i%128),1); frames(&s,32); fm4_note_off(&s); frames(&s,32); }
        CHECK(!s.nonfinite);
    }
    puts("envelopes/lifecycle: 44.1/48/96 kHz, retrigger and mode latching PASS");
}
static void partition(void) {
    fm4_state a,b,other; CHECK(!fm4_init(&a,48000,123)); b=a; other=a;
    float p[16]; memcpy(p,fm4_defaults,sizeof p); p[GATE]=127; p[DRFT]=127;
    fm4_params(&a,p); fm4_params(&b,p); fm4_note_on(&a,55,.8f); fm4_note_on(&b,55,.8f);
    float x[4096],y[4096];
    for(int event=0;event<25;++event) {
        p[ALGO]=(event%8)*16; p[MRPH]=(event*17)%128; p[RAT]=(event*23)%128;
        fm4_params(&a,p); fm4_params(&b,p);
        fm4_render(&a,x,4096);
        int offset=0; while(offset<4096) { int n=1+(offset*7)%12; if(n>4096-offset) n=4096-offset; fm4_render(&b,y+offset,n); offset+=n; }
        CHECK(!memcmp(x,y,sizeof x)); CHECK(!memcmp(&a,&b,sizeof a));
        // Mutate/render a third instance: it must not affect either reference voice.
        fm4_note_on(&other,(float)(event+30),.7f); frames(&other,113); fm4_note_off(&other);
    }
    fm4_state different; CHECK(!fm4_init(&different,48000,124));
    fm4_params(&different,p); fm4_note_on(&different,55,.8f); frames(&different,96000);
    CHECK(a.drift[1].rng!=different.drift[1].rng);
    puts("determinism, block partition invariance, and interleaved voice independence PASS");
}
static uint32_t rng=0x12345678;
static unsigned rnd(void) { rng^=rng<<13; rng^=rng>>17; return rng^=rng<<5; }
static void stress(void) {
    fm4_state s; CHECK(!fm4_init(&s,48000,1)); float p[16]; memcpy(p,fm4_defaults,sizeof p);
    for(int alg=0;alg<8;++alg) {
        for(int k=0;k<16;++k) for(int end=0;end<2;++end) {
            memcpy(p,fm4_defaults,sizeof p); p[ALGO]=alg*16; p[k]=end?127:0;
            fm4_params(&s,p); fm4_note_on(&s,127,1); frames(&s,300); fm4_note_off(&s);
        }
    }
    for(int k=0;k<20000;++k) {
        for(int j=0;j<16;++j) p[j]=(rnd()%16384)/128.0f;
        if(k%7==0) p[rnd()%16]=NAN;
        fm4_params(&s,p);
        if(k%13==0) fm4_note_on(&s,(float)(rnd()%128), (rnd()%128)/127.0f);
        if(k%19==0) fm4_note_off(&s);
        block(&s,12);
        CHECK(s.transition<=s.transition_length); CHECK(s.bank[0].algorithm>=0 && s.bank[0].algorithm<8);
    }
    // Invalid values retain their last valid targets; infinities cannot reach integer conversions.
    float raw=s.raw[COL]; p[COL]=INFINITY; fm4_params(&s,p); CHECK(s.raw[COL]==raw);
    fm4_note_on(&s,NAN,1); block(&s,12); CHECK(!s.nonfinite);
    s.amplitude=NAN; block(&s,12); CHECK(s.nonfinite==1 && !s.stage);
    printf("endpoints/random stress PASS; safety-clamp samples %u\n",s.clamps);
}
static void transitions(void) {
    fm4_state s,reference;float p[16];clean(p);fm4_init(&s,48000,12);fm4_params(&s,p);fm4_note_on(&s,60,1);frames(&s,2000);
    reference=s;fm4_note_on(&s,72,.5f);
    float x[48],y[48];fm4_render(&s,x,48);fm4_render(&reference,y,48);
    for(int i=0;i<48;++i)CHECK(fabsf(x[i]-y[i]*(47-i)/48)<1e-6f);
    CHECK(x[47]==0 && s.note==72 && s.amplitude==0);
    frames(&s,1000);p[ALGO]=16;fm4_params(&s,p);block(&s,1);CHECK(s.transition==239);
    p[ALGO]=112;fm4_params(&s,p);block(&s,100);CHECK(s.bank[1-s.active_bank].algorithm==1);
    frames(&s,139);CHECK(!s.transition && s.bank[s.active_bank].algorithm==1);
    block(&s,1);CHECK(s.transition==239 && s.bank[1-s.active_bank].algorithm==7);
    frames(&s,239);CHECK(!s.transition && s.bank[s.active_bank].algorithm==7);
    puts("retrigger fade shape and bounded latest-target algorithm transition PASS");
}
static void mappings(void) {
    fm4_state s; CHECK(!fm4_init(&s,48000,1)); float p[16]; memcpy(p,fm4_defaults,sizeof p);
    CHECK(s.target[TUNE]==0); CHECK(fabsf(s.target[FM]-8*(powf(2,3*52.0f/127)-1)/7)<1e-5f);
    float ratios[4]; fm4_ratios(72,ratios); CHECK(fabsf(exp2f(ratios[1])-1.414214f)<1e-6f);
    p[FM]=127;p[FDBK]=127;p[TUNE]=0;p[SWP]=64;fm4_params(&s,p);
    CHECK(s.target[FM]==8 && s.target[FDBK]==2.5f && s.target[TUNE]==-100 && s.target[SWP]==0);
    p[TUNE]=127;fm4_params(&s,p);CHECK(s.target[TUNE]==100);
    for(int a=0;a<8;++a) { p[ALGO]=a*16; fm4_params(&s,p);CHECK(s.requested_algorithm==a); }
    p[VEL]=127;fm4_params(&s,p);fm4_note_on(&s,60,.25f);float dark=s.timbre;fm4_leave(&s);
    p[VEL]=0;fm4_params(&s,p);fm4_note_on(&s,60,.25f);CHECK(s.timbre==1 && dark<.36f);
    float err=0;for(int i=-10000;i<10000;++i) { float x=i*.005f;err=fmaxf(err,fabsf(fm4_sin(x)-sinf(x))); } CHECK(err<5e-6f);
    puts("mappings/velocity/lookup accuracy PASS");
}
int main(void) {
    CHECK(sizeof(fm4_state)<4096); CHECK(rmp_entry.api==2); CHECK(rmp_entry.state_bytes==sizeof(fm4_state));
    rmp_host host={2,48000,12,0,0,{0}};
    CHECK(rmp_entry.init(&host)==0); host.api=1; CHECK(rmp_entry.init(&host)!=0); host.api=2;
    fm4_state s; memset(&s,0,sizeof s); rmp_entry.voice_init(&s); rmp_voice_enter_of(&rmp_entry)(&s);
    float out[48000],p[16]; clean(p);
    rmp_entry.params(&s,p); rmp_entry.note_on(&s,69,1); rmp_entry.render(&s,out,48000);
    double first=-1,last=0; int crosses=0;
    for(int i=4801;i<48000;++i) if(out[i-1]<0 && out[i]>=0) { double t=i-1-out[i-1]/(double)(out[i]-out[i-1]); if(first<0)first=t;last=t;++crosses; }
    double hz=(crosses-1)*48000/(last-first); CHECK(fabs(1200*log2(hz/440))<1);
    int crossings=0; for(int i=4801;i<48000;++i) if(out[i-1]<0 && out[i]>=0) crossings++;
    CHECK(abs(crossings-396)<=1); CHECK(s.amplitude==1); CHECK(s.clamps==0);
    rmp_entry.note_off(&s); frames(&s,48000); CHECK(!s.stage); block(&s,12);
    rmp_voice_leave_of(&rmp_entry)(&s); CHECK(!s.stage && s.dc_y==0);
    printf("STACK/API smoke PASS; state %zu bytes; clean A4 crossings %d/0.9s\n",sizeof s,crossings);
    routing(); lifecycle(); partition(); mappings(); transitions(); stress();
    double maxdc=0;
    for(int a=0;a<8;++a) {
        fm4_init(&s,48000,7);memcpy(p,fm4_defaults,sizeof p);p[ALGO]=a*16;p[FM]=127;p[FDBK]=127;p[GATE]=127;p[BODY]=0;
        fm4_params(&s,p);fm4_note_on(&s,60,1);frames(&s,48000);
        double sum=0; for(int j=0;j<2;++j) {fm4_render(&s,out,48000);for(int i=0;i<48000;++i)sum+=out[i];}
        double dc=fabs(sum/96000);if(dc>maxdc)maxdc=dc;CHECK(dc<.001);CHECK(!s.nonfinite);
        p[LEV]=0;fm4_params(&s,p);frames(&s,48000);fm4_render(&s,out,12);for(int i=0;i<12;++i)CHECK(out[i]==0);
    }
    printf("sustained DC/level-zero silence PASS; max DC %.8f; sine error %.5f cents\n",maxdc,1200*log2(hz/440));
    return 0;
}
