// Original FM4 API 2 adapter. SPDX-License-Identifier: MIT
#include "rmp/rmp_api.h"
#include "fm4_dsp.h"
#include <math.h>
// Written only during init (before voice callbacks); immutable while loaded.
static float sample_rate;
static int32_t init(const rmp_host *h) {
    if(!h || h->api!=2 || !isfinite(h->sample_rate) || h->sample_rate<8000 || h->sample_rate>768000 || h->max_block<1) return 1;
    sample_rate=h->sample_rate; return 0;
}
static void voice_init(void *p) { (void)fm4_init(p,sample_rate,FM4_SEED); }
static void enter(void *p) { (void)p; }
static void leave(void *p) { fm4_leave(p); }
static void on(void *p,float n,float v) { fm4_note_on(p,n,v); }
static void off(void *p) { fm4_note_off(p); }
static void params(void *p,const float *v) { fm4_params(p,v); }
static void render(void *p,float *out,int32_t n) { fm4_render(p,out,n); }
RMP_ENTRY const rmp_plugin rmp_entry = {
    RMP_PLUGIN_MAGIC,2,sizeof(fm4_state),FM4_CPU_PERMILLE,
    init,voice_init,on,off,params,render,
    {0,0,RMP_FN(enter),RMP_FN(leave),0,0,0,0}
};
