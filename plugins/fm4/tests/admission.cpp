// Exercise actual host admission; never equate manifest capability with admitted voices.
// SPDX-License-Identifier: MIT
#include "inst_alloc.h"
#include "machines.h"
#include "rmp_format.h"
#include "fm4_dsp.h"
#include <cassert>
#include <cstdio>
#include <initializer_list>
int main() {
    unsigned flags=RMP_HF_INSTANCES | (3u<<RMP_HF_VOICES_SHIFT);
    auto d=core::inst::user_decl(0x50,flags,FM4_CPU_PERMILLE,sizeof(fm4_state));
    assert(d.max_voices==4 && d.def_voices==4);
    for(int requested: {1,2,4}) {
        core::seq::PolyCfg cfg{};cfg.voices=requested;
        auto r=core::inst::resolve(cfg,d);
        assert(r.instances==1 && r.lanes==1 && r.clamped==(requested>1));
        std::printf("requested %d, admitted %d, declared mean %.2f%% peak %.2f%%; admission guard preserved\n",requested,r.instances,d.cpu_mean/100.,d.cpu_peak/100.);
    }
    // Threshold arithmetic, not a declaration for FM4.
    auto threshold=core::inst::user_decl(0x50,flags,1011,sizeof(fm4_state));
    core::seq::PolyCfg cfg{};cfg.voices=4;
    assert(core::inst::resolve(cfg,threshold).instances==4);
}
