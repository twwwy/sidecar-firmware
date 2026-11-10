// Original FM4 core. SPDX-License-Identifier: MIT
#include "fm4_dsp.h"
#include "fm4_tables.h"
#include <math.h>
#include <string.h>
#define PI2 6.2831853071795864769f
#define LOG2E 1.4426950408889634f
#ifndef FM4_REFERENCE_FACTOR
#define CONTROL_STEPS 16
#define RATE_FACTOR 1
#else
#define CONTROL_STEPS (16 * FM4_REFERENCE_FACTOR)
#define RATE_FACTOR FM4_REFERENCE_FACTOR
#ifndef FM4_REFERENCE_DELAY
#define FM4_REFERENCE_DELAY FM4_REFERENCE_FACTOR
#endif
#endif
#define BIT(i) (1u << (i))
// Destination-indexed source masks. All current edges point from a higher index.
const fm4_route fm4_routes[8] = {
    {{2,4,8,0}, {0,0,0,8}, 1,0},
    {{2,0,8,0}, {0,2,0,8}, 5,0},
    {{2,4,0,0}, {0,0,4,0}, 9,3},
    {{14,0,0,0},{0,2,4,8}, 1,0},
    {{0,8,8,0}, {0,0,0,8}, 7,0},
    {{2,0,8,0}, {0,4,0,1}, 5,0},
    {{2,4,8,0}, {0,0,0,1}, 1,0},
    {{0,0,0,0}, {1,2,4,8},15,0}
};
const float fm4_defaults[16] = {0,72,64,52,24,96,64,0,0,64,72,8,80,64,0,100};
static const float z[4] = {-1,-1.0f/3,1.0f/3,1};
static float clamp(float x, float lo, float hi) { return x < lo ? lo : x > hi ? hi : x; }
static float bipolar(float p) { return (p-64) / (p < 64 ? 64 : 63); }
static uint32_t random32(uint32_t *p) {
    uint32_t x=*p; x ^= x<<13; x ^= x>>17; x ^= x<<5; return *p=x;
}
static float random_signed(uint32_t *p) { return (random32(p)>>8)*(2.0f/16777215.0f)-1; }
float fm4_exp2(float x) {
    // Callers constrain x; explicit range guard prevents undefined float-to-int conversion.
    if (!isfinite(x)) return 1;
    x=clamp(x,-30,30);
    int whole=(int)x; if (x<whole) --whole;
    float u=(x-whole)*1024;
    int k=(int)u; if (k>1023) k=1023;
    union { uint32_t bits; float f; } scale;
    scale.bits=(uint32_t)(whole+127)<<23;
    return scale.f*(fm4_exp2_fraction[k]+(u-k)*(fm4_exp2_fraction[k+1]-fm4_exp2_fraction[k]));
}
float fm4_sin(float radians) {
    // Inputs are bounded (phase plus finite modulation), so conversion is safe.
    if (!isfinite(radians) || fabsf(radians)>1000000) return 0;
#ifdef FM4_REFERENCE_FACTOR
    return sinf(radians); // offline reference only; never in the production image
#endif
    float cycles=radians/PI2;
    int whole=(int)cycles; if (cycles<whole) --whole;
    float u=(cycles-whole)*4096;
    int k=(int)u;
    if (k>=4096) return fm4_sine[0];
    return fm4_sine[k]+(u-k)*(fm4_sine[k+1]-fm4_sine[k]);
}
void fm4_ratios(float raw, float out[4]) {
    static const float land[8]={0,18,36,54,72,90,108,127};
    static const float ratios[8][4]={
        {1,1,1,1},{1,2,3,4},{1,3,5,7},{1,.5f,1.5f,2},
        {1,1.414214f,2.414214f,3.141593f},{1,1.003f,.997f,2.007f},
        {1,1.618034f,2.718282f,4.236068f},{1,.25f,1.414214f,5.196152f}
    };
    raw=isfinite(raw)?clamp(raw,0,127):0;
    int k=0; while (k<6 && raw>land[k+1]) ++k;
    float t=clamp(((raw-land[k])/(land[k+1]-land[k])-.1f)/.8f,0,1);
    t=t*t*(3-2*t);
    for (int i=0;i<4;++i) out[i]=log2f(ratios[k][i])*(1-t)+log2f(ratios[k+1][i])*t;
}
static float mapped(int i,float p) {
    float x=p/127;
    switch (i) {
    case COL: case SWP: return bipolar(p);
    case FM: return 8*(fm4_exp2(3*x)-1)/7;
    case FDBK: return 2.5f*x*x;
    case DEC: return .020f*powf(200,x);
    case ATK: return .001f*powf(2000,x);
    case REL: return .010f*powf(800,x);
    case DRFT: return 30*x*x;
    case TUNE: return 100*bipolar(p);
    case LEV: return .8f*x*x;
    default: return x;
    }
}
void fm4_params(fm4_state *s,const float p[16]) {
    for (int i=0;i<16;++i) if (isfinite(p[i])) {
        float raw=clamp(p[i],0,127);
        if (raw==s->raw[i]) continue;
        s->raw[i]=raw;
        s->target[i]=mapped(i,raw);
        if(i!=ALGO && i!=RAT && i!=VEL && i!=GATE) s->smooth_mask|=BIT(i);
        if (i==RAT) { fm4_ratios(raw,s->ratio_target); s->ratio_mask=15; }
    }
    s->requested_algorithm=((int)s->raw[ALGO]*8)>>7;
}
static void configure_bank(fm4_state *s,fm4_bank *b,int ramp) {
    fm4_bank old=*b;
    const fm4_route *r=&fm4_routes[b->algorithm];
    float col=s->smooth[COL],m=s->smooth[MRPH],body=s->smooth[BODY];
    float amount=(1-body)*(1-body), sum=0;
    static const float morph_edge[4]={0,-.20f,.25f,-.15f};
    for (int i=0;i<4;++i) {
        b->weight[i]=(r->carriers&BIT(i)) ? fm4_exp2(col*z[i])*(1+.2f*m*z[i])*(i==r->anchor?1+3*body:1):0;
        sum+=b->weight[i];
        int count=0; for(int j=0;j<4;++j) count+=(r->current[i]>>j)&1;
        for (int j=0;j<4;++j) {
            b->edge[i][j]=(r->current[i]&BIT(j)) ? fm4_exp2(col*z[j])*(1+m*morph_edge[j])/(float)count:0;
            b->feedback[i][j]=(r->delayed[i]&BIT(j)) ? s->smooth[FDBK]*(1-.2f*m):0;
            if (b->algorithm==7) b->feedback[i][j]*=fm4_exp2(.5f*col*z[i]);
            if (i==r->anchor) { b->edge[i][j]*=amount; b->feedback[i][j]*=amount; }
        }
    }
    for (int i=0;i<4;++i) {
        b->weight[i]/=sum;
        b->weight_step[i]=ramp?(b->weight[i]-old.weight[i])/CONTROL_STEPS:0;
        if(ramp) b->weight[i]=old.weight[i];
        for(int j=0;j<4;++j) {
            b->edge_step[i][j]=ramp?(b->edge[i][j]-old.edge[i][j])/CONTROL_STEPS:0;
            b->feedback_step[i][j]=ramp?(b->feedback[i][j]-old.feedback[i][j])/CONTROL_STEPS:0;
            if(ramp) { b->edge[i][j]=old.edge[i][j]; b->feedback[i][j]=old.feedback[i][j]; }
        }
    }
}
float fm4_bank_tick(fm4_bank *b,const float inc[4],float index,float activity) {
    float old[4],y[4]={0},mix=0;
#ifdef FM4_REFERENCE_FACTOR
    memcpy(old,b->delay[b->delay_position],sizeof old);
#else
    memcpy(old,b->previous,sizeof old);
#endif
    // Fixed graphs avoid matrix traversal. Tests check these against independent equations.
    float modulator_gain=.35f+.65f*activity;
    switch(b->algorithm) {
    case 0:
        y[3]=modulator_gain*fm4_sin(b->phase[3]+(0+(index*b->edge[3][3]*y[3]+b->feedback[3][3]*old[3])));
        y[2]=modulator_gain*fm4_sin(b->phase[2]+(0+(index*b->edge[2][3]*y[3]+b->feedback[2][3]*old[3])));
        y[1]=modulator_gain*fm4_sin(b->phase[1]+(0+(index*b->edge[1][2]*y[2]+b->feedback[1][2]*old[2])));
        y[0]=1*fm4_sin(b->phase[0]+(0+(index*b->edge[0][1]*y[1]+b->feedback[0][1]*old[1])));
        break;
    case 1:
        y[3]=modulator_gain*fm4_sin(b->phase[3]+(0+(index*b->edge[3][3]*y[3]+b->feedback[3][3]*old[3])));
        y[2]=1*fm4_sin(b->phase[2]+(0+(index*b->edge[2][3]*y[3]+b->feedback[2][3]*old[3])));
        y[1]=modulator_gain*fm4_sin(b->phase[1]+(0+(index*b->edge[1][1]*y[1]+b->feedback[1][1]*old[1])));
        y[0]=1*fm4_sin(b->phase[0]+(0+(index*b->edge[0][1]*y[1]+b->feedback[0][1]*old[1])));
        break;
    case 2:
        y[3]=1*fm4_sin(b->phase[3]+0);
        y[2]=modulator_gain*fm4_sin(b->phase[2]+(0+(index*b->edge[2][2]*y[2]+b->feedback[2][2]*old[2])));
        y[1]=modulator_gain*fm4_sin(b->phase[1]+(0+(index*b->edge[1][2]*y[2]+b->feedback[1][2]*old[2])));
        y[0]=1*fm4_sin(b->phase[0]+(0+(index*b->edge[0][1]*y[1]+b->feedback[0][1]*old[1])));
        break;
    case 3:
        y[3]=modulator_gain*fm4_sin(b->phase[3]+(0+(index*b->edge[3][3]*y[3]+b->feedback[3][3]*old[3])));
        y[2]=modulator_gain*fm4_sin(b->phase[2]+(0+(index*b->edge[2][2]*y[2]+b->feedback[2][2]*old[2])));
        y[1]=modulator_gain*fm4_sin(b->phase[1]+(0+(index*b->edge[1][1]*y[1]+b->feedback[1][1]*old[1])));
        y[0]=1*fm4_sin(b->phase[0]+(((0+(index*b->edge[0][1]*y[1]+b->feedback[0][1]*old[1]))+(index*b->edge[0][2]*y[2]+b->feedback[0][2]*old[2]))+(index*b->edge[0][3]*y[3]+b->feedback[0][3]*old[3])));
        break;
    case 4:
        y[3]=modulator_gain*fm4_sin(b->phase[3]+(0+(index*b->edge[3][3]*y[3]+b->feedback[3][3]*old[3])));
        y[2]=1*fm4_sin(b->phase[2]+(0+(index*b->edge[2][3]*y[3]+b->feedback[2][3]*old[3])));
        y[1]=1*fm4_sin(b->phase[1]+(0+(index*b->edge[1][3]*y[3]+b->feedback[1][3]*old[3])));
        y[0]=1*fm4_sin(b->phase[0]+0);
        break;
    case 5:
        y[3]=modulator_gain*fm4_sin(b->phase[3]+(0+(index*b->edge[3][0]*y[0]+b->feedback[3][0]*old[0])));
        y[2]=1*fm4_sin(b->phase[2]+(0+(index*b->edge[2][3]*y[3]+b->feedback[2][3]*old[3])));
        y[1]=modulator_gain*fm4_sin(b->phase[1]+(0+(index*b->edge[1][2]*y[2]+b->feedback[1][2]*old[2])));
        y[0]=1*fm4_sin(b->phase[0]+(0+(index*b->edge[0][1]*y[1]+b->feedback[0][1]*old[1])));
        break;
    case 6:
        y[3]=modulator_gain*fm4_sin(b->phase[3]+(0+(index*b->edge[3][0]*y[0]+b->feedback[3][0]*old[0])));
        y[2]=modulator_gain*fm4_sin(b->phase[2]+(0+(index*b->edge[2][3]*y[3]+b->feedback[2][3]*old[3])));
        y[1]=modulator_gain*fm4_sin(b->phase[1]+(0+(index*b->edge[1][2]*y[2]+b->feedback[1][2]*old[2])));
        y[0]=1*fm4_sin(b->phase[0]+(0+(index*b->edge[0][1]*y[1]+b->feedback[0][1]*old[1])));
        break;
    case 7:
        y[3]=1*fm4_sin(b->phase[3]+(0+(index*b->edge[3][3]*y[3]+b->feedback[3][3]*old[3])));
        y[2]=1*fm4_sin(b->phase[2]+(0+(index*b->edge[2][2]*y[2]+b->feedback[2][2]*old[2])));
        y[1]=1*fm4_sin(b->phase[1]+(0+(index*b->edge[1][1]*y[1]+b->feedback[1][1]*old[1])));
        y[0]=1*fm4_sin(b->phase[0]+(0+(index*b->edge[0][0]*y[0]+b->feedback[0][0]*old[0])));
        break;
    default: return 0;
    }
    for(int i=3;i>=0;--i) {
        b->phase[i]+=inc[i]; if(b->phase[i]>=PI2) b->phase[i]-=PI2;
        mix+=b->weight[i]*y[i];
    }
#ifdef FM4_REFERENCE_FACTOR
    memcpy(b->delay[b->delay_position],y,sizeof y);
    b->delay_position=(b->delay_position+1)%FM4_REFERENCE_DELAY;
#endif
    memcpy(b->previous,y,sizeof y);
    return mix;
}
static void coefficients(fm4_state *s,int ramp) {
    static const float morph_cents[4]={0,17,-23,31};
    static const float drift_scale[4]={.2f,1,.7f,1};
    for(int i=0;i<4;++i) {
        float pitch=(s->note-69)/12+s->ratio[i]+(s->smooth[TUNE]+morph_cents[i]*s->smooth[MRPH]+
                     s->smooth[DRFT]*drift_scale[i]*s->drift[i].value)/1200;
        float next=PI2*fminf(440*fm4_exp2(pitch)/s->rate,.2f/RATE_FACTOR);
        s->inc_step[i]=ramp?(next-s->inc[i])/CONTROL_STEPS:0;
        if(!ramp) s->inc[i]=next;
    }
    if(s->cached_atk!=s->smooth[ATK]) {
        s->cached_atk=s->smooth[ATK]; s->attack_step=1/(s->cached_atk*s->rate);
    }
    if(s->cached_dec!=s->smooth[DEC]) {
        s->cached_dec=s->smooth[DEC];
        s->body_keep=expf(-1.38629436112f/(s->cached_dec*s->rate));
        s->contour_keep=expf(-6.907755279f/(s->cached_dec*s->rate));
    }
    if(s->cached_rel!=s->smooth[REL]) {
        s->cached_rel=s->smooth[REL]; s->release_keep=expf(-11.512925465f/(s->cached_rel*s->rate));
    }
    configure_bank(s,&s->bank[s->active_bank],ramp);
    if(s->transition) configure_bank(s,&s->bank[1-s->active_bank],ramp);
}
int fm4_init(fm4_state *s,float rate,uint32_t seed) {
    if(!s || !isfinite(rate) || rate<8000 || rate>768000) return 1;
    memset(s,0,sizeof *s); s->rate=rate; s->seed=seed?seed:FM4_SEED;
    s->dc_r=expf(-PI2*10/rate);
    s->transition_length=(uint32_t)ceil(.005*(double)rate);
    s->retrigger_length=(uint32_t)ceil(.001*(double)rate);
    static const float tau[16]={0,.010f,.005f,.003f,.005f,.005f,.010f,.010f,.010f,.010f,.005f,.050f,0,.005f,0,.005f};
    static const float periods[4]={.73f,1.07f,1.61f,2.29f};
    for (int i=0;i<16;++i) { s->raw[i]=-1; s->slew[i]=tau[i]>0?-expm1f(-1/(tau[i]*rate)):1; }
    fm4_params(s,fm4_defaults);
    memcpy(s->smooth,s->target,sizeof s->target);
    memcpy(s->ratio,s->ratio_target,sizeof s->ratio); s->smooth_mask=0; s->ratio_mask=0;
    for (int i=0;i<4;++i) {
        s->drift[i].rng=s->seed ^ (0x9e3779b9u*(i+1)); if(!s->drift[i].rng) s->drift[i].rng=1;
        s->drift[i].length=(uint32_t)(periods[i]*rate+.5f);
        s->drift[i].to=random_signed(&s->drift[i].rng);
    }
    coefficients(s,0); return 0;
}
void fm4_leave(fm4_state *s) {
    s->stage=0; s->held=0; s->retrigger=0; s->transition=0;
    s->amplitude=0; s->contour=0; s->dc_x=0; s->dc_y=0;
    for(int b=0;b<2;++b) { memset(s->bank[b].phase,0,sizeof s->bank[b].phase); memset(s->bank[b].previous,0,sizeof s->bank[b].previous); }
}
static void start_note(fm4_state *s,float note,float vel,int held) {
    s->note=note; s->velocity=vel; s->held=held; s->gated=s->raw[GATE]>=64;
    s->release_progress=0; s->amplitude=0; s->contour=1; s->stage=1; s->stage_samples=0;
    s->timbre=fm4_exp2(2*s->target[VEL]*(vel-1));
    s->transition=0; s->active_bank=0;
    for(int b=0;b<2;++b) {
        memset(s->bank[b].phase,0,sizeof s->bank[b].phase); memset(s->bank[b].previous,0,sizeof s->bank[b].previous);
        s->bank[b].algorithm=s->requested_algorithm;
#ifdef FM4_REFERENCE_FACTOR
        memset(s->bank[b].delay,0,sizeof s->bank[b].delay); s->bank[b].delay_position=0;
#endif
    }
    memcpy(s->smooth,s->target,sizeof s->target); memcpy(s->ratio,s->ratio_target,sizeof s->ratio); s->smooth_mask=0; s->ratio_mask=0;
    s->dc_x=0; s->dc_y=0; s->control_phase=0; ++s->note_count;
    coefficients(s,0);
    if(s->gated && !held) s->stage=4; // note-off while waiting: do not resurrect a held note
    if(vel==0) fm4_leave(s);
}
void fm4_note_on(fm4_state *s,float note,float velocity) {
    if(!isfinite(note)||!isfinite(velocity)) return;
    note=clamp(note,0,127); velocity=clamp(velocity,0,1);
    if(s->stage || fabsf(s->dc_y)>1e-7f) {
        s->pending_note=note; s->pending_velocity=velocity; s->pending_held=1;
        if(!s->retrigger) s->retrigger=s->retrigger_length;
    } else start_note(s,note,velocity,1);
}
void fm4_note_off(fm4_state *s) {
    s->held=0; if(s->retrigger) s->pending_held=0;
    if(s->gated && s->stage && s->stage!=4) { s->stage=4; s->stage_samples=0; s->release_progress=0; }
}
static void drift_tick(fm4_state *s) {
    for(int i=0;i<4;++i) {
        fm4_drift *d=&s->drift[i];
        float t=(float)d->position/d->length; t=t*t*(3-2*t);
        d->value=d->from+(d->to-d->from)*t;
        if(++d->position>=d->length) { d->position=0; d->from=d->to; d->to=random_signed(&d->rng); }
    }
}
void fm4_stats(const fm4_state *s,float peaks[2],uint32_t counters[2]) {
    peaks[0]=s->pre_peak; peaks[1]=s->post_peak;
    counters[0]=s->clamps; counters[1]=s->nonfinite;
}
void fm4_render(fm4_state *s,float *out,int32_t n) {
    if(!s || !out || n<=0) return;
    for(int32_t k=0;k<n;++k) {
        if(!s->stage && !s->retrigger) { out[k]=0; continue; }
        if(!isfinite(s->amplitude)||!isfinite(s->contour)||!isfinite(s->dc_y)) {
            ++s->nonfinite; fm4_leave(s); out[k]=0; continue;
        }
        unsigned moving=s->smooth_mask;
        while(moving) {
            int i=__builtin_ctz(moving); moving&=moving-1;
            s->smooth[i]+=(s->target[i]-s->smooth[i])*s->slew[i];
            if(fabsf(s->smooth[i]-s->target[i])<1e-7f) { s->smooth[i]=s->target[i]; s->smooth_mask&=~BIT(i); }
        }
        moving=s->ratio_mask;
        while(moving) {
            int i=__builtin_ctz(moving); moving&=moving-1;
            s->ratio[i]+=(s->ratio_target[i]-s->ratio[i])*s->slew[RAT];
            if(fabsf(s->ratio[i]-s->ratio_target[i])<1e-7f) { s->ratio[i]=s->ratio_target[i]; s->ratio_mask&=~BIT(i); }
        }
        drift_tick(s);
        fm4_bank *a=&s->bank[s->active_bank],*b=&s->bank[1-s->active_bank];
        if(!s->transition && a->algorithm!=s->requested_algorithm && !s->retrigger) {
            *b=*a; b->algorithm=s->requested_algorithm; configure_bank(s,b,0); s->transition=s->transition_length;
        }
        if(!s->control_phase) coefficients(s,1);
        s->control_phase=(s->control_phase+1)%CONTROL_STEPS;
        for(int i=0;i<4;++i) {
            s->inc[i]+=s->inc_step[i];
            for(int bank=0;bank<(s->transition?2:1);++bank) {
                fm4_bank *q=bank?b:a;
                q->weight[i]+=q->weight_step[i];
                const fm4_route *route=&fm4_routes[q->algorithm];
                unsigned sources=route->current[i]|route->delayed[i];
                while(sources) {
                    int j=__builtin_ctz(sources); sources&=sources-1;
                    q->edge[i][j]+=q->edge_step[i][j];
                    q->feedback[i][j]+=q->feedback_step[i][j];
                }
            }
        }
        float sweep=s->smooth[SWP],ab=fabsf(sweep);
        float activity=1-ab+ab*(sweep>=0?s->contour:1-s->contour);
        float idx=fminf(16,s->smooth[FM]*s->timbre*fm4_exp2(2*sweep*(2*s->contour-1)));
        float sample=fm4_bank_tick(a,s->inc,idx,activity);
        if(s->transition) {
            float t=1-(float)s->transition/s->transition_length;
            sample=sample*(1-t)+fm4_bank_tick(b,s->inc,idx,activity)*t;
            if(!--s->transition) s->active_bank=1-s->active_bank;
        }
        // Envelope amplitude is normalized; velocity is applied once, independently of VEL.
        if(s->stage==1) {
            s->amplitude+=s->attack_step;
            if(s->amplitude>=1) { s->amplitude=1; s->stage=s->gated?2:3; s->stage_samples=0; }
        } else if(s->stage==3) {
            s->amplitude*=s->body_keep;
            if(s->amplitude<=.25f) { s->stage=4; s->stage_samples=0; s->release_progress=0; }
        } else if(s->stage==4) {
            s->amplitude*=s->release_keep;
            // Amplitude continues from its current value when REL changes.
        }
        if(s->stage==4) {
            // Time-normalized progress, independent of the amplitude at note-off.
            s->stage_samples++;
            s->release_progress+=1/(s->smooth[REL]*s->rate);
        }
        s->contour*=s->contour_keep; if(s->contour<1e-7f) s->contour=0;
        sample*=s->amplitude*s->velocity;
        float hp=sample-s->dc_x+s->dc_r*s->dc_y;
        s->dc_x=sample; s->dc_y=fabsf(hp)<1e-12f?0:hp;
        sample=hp*s->smooth[LEV];
        if(s->retrigger) sample*=(float)(s->retrigger-1)/s->retrigger_length;
        if(!isfinite(sample)) { ++s->nonfinite; fm4_leave(s); sample=0; }
        s->pre_peak=fmaxf(s->pre_peak,fabsf(sample));
        if(fabsf(sample)>.95f) { ++s->clamps; sample=clamp(sample,-.95f,.95f); }
        s->post_peak=fmaxf(s->post_peak,fabsf(sample));
        out[k]=sample;
        if(s->retrigger && !--s->retrigger) start_note(s,s->pending_note,s->pending_velocity,s->pending_held);
        else if(s->stage==4 && s->release_progress>=1) fm4_leave(s);
    }
}
_Static_assert(sizeof(fm4_state)<4096,"FM4 state budget");
