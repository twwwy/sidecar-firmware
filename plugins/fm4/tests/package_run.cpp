// Native Apple-silicon execution of the exact RMPA bytes through the repo loader.
// This is NOT Pi hardware evidence. SPDX-License-Identifier: MIT
#include "rmp_api.h"
#include "rmp_loader.h"
#include "rmp_manifest.h"
#include <libkern/OSCacheControl.h>
#include <pthread.h>
#include <sys/mman.h>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>
#define CHECK(x) do { if(!(x)){std::fprintf(stderr,"FAIL %d: %s\n",__LINE__,#x);std::exit(1);} }while(0)
static bool read(void *v,uint32_t off,uint8_t *dst,uint32_t n) {
    const auto &b=*static_cast<std::vector<uint8_t>*>(v);
    if(off>b.size() || n>b.size()-off)return false;
    std::memcpy(dst,b.data()+off,n);return true;
}
int main(int argc,char **argv) {
    CHECK(argc==2);FILE *f=std::fopen(argv[1],"rb");CHECK(f);
    CHECK(!std::fseek(f,0,SEEK_END));long len=std::ftell(f);CHECK(len>0);std::rewind(f);
    std::vector<uint8_t> bytes((size_t)len);CHECK(std::fread(bytes.data(),1,bytes.size(),f)==bytes.size());std::fclose(f);
    rmp_info info; rmp_host_target=RMP_TARGET_ARM32;CHECK(rmp_verify(bytes.data(),bytes.size(),&info)==RMP_ERR_TARGET);
    rmp_host_target=RMP_TARGET_A64;CHECK(rmp_verify(bytes.data(),bytes.size(),&info)==RMP_OK);
    bytes[100]^=1;CHECK(rmp_verify(bytes.data(),bytes.size(),&info)==RMP_ERR_CRC);bytes[100]^=1;
    CHECK(rmp_verify(bytes.data(),bytes.size(),&info)==RMP_OK);
    rmp_desc desc;CHECK(rmp_manifest_read(&info,read,&bytes,&desc));CHECK(desc.nknobs==16 && desc.page2==1 && desc.pre==-1);
    CHECK(rmp_hf_max_voices(info.flags)==4);
    size_t code_size=info.data_start;
    size_t data_size=(info.image_size-info.data_start+info.bss_size+4095)&~size_t(4095);if(!data_size)data_size=4096;
    auto code=(uint8_t*)mmap(nullptr,code_size,PROT_READ|PROT_WRITE|PROT_EXEC,MAP_PRIVATE|MAP_ANON|MAP_JIT,-1,0);
    auto data=(uint8_t*)mmap(code==MAP_FAILED?nullptr:code+code_size,data_size,PROT_READ|PROT_WRITE,MAP_PRIVATE|MAP_ANON,-1,0);
    CHECK(code!=MAP_FAILED && data!=MAP_FAILED);
    pthread_jit_write_protect_np(0);
    auto status=rmp_place_a64(&info,code,(uintptr_t)code,data,(uintptr_t)data);
    pthread_jit_write_protect_np(1);CHECK(status==RMP_OK);sys_icache_invalidate(code,code_size);
    auto plugin=(const rmp_plugin*)(code+info.entry_offset);
    rmp_host host={2,48000,12,nullptr,nullptr,{}};
    CHECK(plugin->magic==RMP_PLUGIN_MAGIC && plugin->api==2 && plugin->state_bytes<4096);CHECK(plugin->init(&host)==0);
    float p[16];for(int i=0;i<16;++i)p[desc.knob[i].p-1]=desc.knob[i].def;
    double energy=0;float peak=0;
    // Four separate states exercise package instance isolation, without claiming host poly admission.
    std::vector<uint64_t> states[4];
    for(auto &s:states){s.resize((plugin->state_bytes+7)/8+4,0);plugin->voice_init(s.data());rmp_voice_enter_of(plugin)(s.data());}
    for(int algorithm=0;algorithm<8;++algorithm) {
        p[0]=algorithm*16;p[14]=127;p[3]=127;p[4]=127;p[10]=0;p[11]=127;
        for(int i=0;i<4;++i){plugin->params(states[i].data(),p);plugin->note_on(states[i].data(),48+i*19,.85f);}
        for(int block=0;block<2000;++block) for(auto &s:states) {
            float output[12];plugin->render(s.data(),output,12);
            for(float x:output){CHECK(std::isfinite(x)&&std::abs(x)<=.950001f);energy+=x*x;peak=std::max(peak,std::abs(x));}
        }
        for(auto &s:states)plugin->note_off(s.data());
    }
    for(auto &s:states){rmp_voice_leave_of(plugin)(s.data());float output[12];plugin->render(s.data(),output,12);for(float x:output)CHECK(x==0);}
    CHECK(energy>1);
    std::printf("RMPA exact-package Mac execution PASS: eight algorithms, four independent states; peak %.6f; state %u; four-instance capability (admission tested separately)\n",peak,plugin->state_bytes);
    munmap(code,code_size);munmap(data,data_size);
}
