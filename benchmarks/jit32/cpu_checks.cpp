#include "device/r4300/jit/n64emujit.h"
extern "C" {
#include "device/r4300/r4300_core.h"
void core_log(int,const char*,...) {}
}
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <random>
#include <vector>
static void require(bool value,const char* message) {
    if(!value){std::fprintf(stderr,"CPU JIT32: %s\n",message);std::exit(1);}
}
static int step(r4300_core* state,void*,unsigned index,unsigned phase) {
    require(phase!=2,"arithmetic must execute as native code");
    if(phase==1){state->regs[0]=0;++state->retired_ops;state->instruction_pc.addr=4*(index+1);}
    return 1;
}
static uint64_t word(uint64_t value){return uint64_t(int64_t(int32_t(value)));}
static void reference(r4300_core& state,uint32_t op) {
    unsigned major=op>>26,fn=op&63,rs=(op>>21)&31,rt=(op>>16)&31,rd=(op>>11)&31,sa=(op>>6)&31;
    uint64_t a=state.regs[rs],b=state.regs[rt],result=0;bool write=true;
    if(major){rd=rt;switch(major){
        case 9:result=word(a+int16_t(op));break;case 25:result=a+int16_t(op);break;
        case 10:result=int64_t(a)<int64_t(int16_t(op));break;case 11:result=a<uint64_t(int64_t(int16_t(op)));break;
        case 12:result=a&uint16_t(op);break;case 13:result=a|uint16_t(op);break;case 14:result=a^uint16_t(op);break;
        case 15:result=word(uint32_t(op)<<16);break;default:std::abort();
    }}else switch(fn){
        case 0:result=word(uint32_t(b)<<sa);break;case 2:result=word(uint32_t(b)>>sa);break;case 3:result=word(int64_t(b)>>sa);break;
        case 4:result=word(uint32_t(b)<<(a&31));break;case 6:result=word(uint32_t(b)>>(a&31));break;case 7:result=word(int64_t(b)>>(a&31));break;
        case 15:write=false;break;case 16:result=state.hi;break;case 18:result=state.lo;break;
        case 17:state.hi=a;write=false;break;case 19:state.lo=a;write=false;break;
        case 20:result=b<<(a&63);break;case 22:result=b>>(a&63);break;case 23:result=uint64_t(int64_t(b)>>(a&63));break;
        case 33:result=word(a+b);break;case 35:result=word(a-b);break;
        case 36:result=a&b;break;case 37:result=a|b;break;case 38:result=a^b;break;case 39:result=~(a|b);break;
        case 42:result=int64_t(a)<int64_t(b);break;case 43:result=a<b;break;case 45:result=a+b;break;case 47:result=a-b;break;
        case 56:result=b<<sa;break;case 58:result=b>>sa;break;case 59:result=uint64_t(int64_t(b)>>sa);break;
        case 60:result=b<<(sa+32);break;case 62:result=b>>(sa+32);break;case 63:result=uint64_t(int64_t(b)>>(sa+32));break;
        default:std::abort();
    }
    if(write&&rd)state.regs[rd]=int64_t(result);state.regs[0]=0;
}
static unsigned linked_count;
static n64emujit_entry linked_body;
static n64emujit_entry link(r4300_core*){return ++linked_count<100000?linked_body:nullptr;}
int main() {
    auto* jit=n64emujit_create();require(jit,"code arena allocation");
    auto actual=std::make_unique<r4300_core>(),expected=std::make_unique<r4300_core>();
    std::mt19937_64 random(0x33324a4954);std::vector<uint32_t> instructions;
    for(unsigned major:{9u,10u,11u,12u,13u,14u,15u,25u})
        for(unsigned imm:{0u,1u,32767u,32768u,65535u})
            for(unsigned rt:{0u,1u,2u})instructions.push_back((major<<26)|(1<<21)|(rt<<16)|imm);
    for(unsigned fn:{0u,2u,3u,4u,6u,7u,15u,16u,17u,18u,19u,20u,22u,23u,33u,35u,36u,37u,38u,39u,42u,43u,45u,47u,56u,58u,59u,60u,62u,63u})
        for(unsigned shift:{0u,1u,15u,31u})for(unsigned dest:{0u,1u,2u,3u})
            instructions.push_back((1<<21)|(2<<16)|(dest<<11)|(shift<<6)|fn);
    unsigned cases=0;
    for(uint32_t op:instructions) {
        require(n64emujit_has_native(&op,1,0,0,0),"native opcode selection");
        auto function=n64emujit_compile(jit,nullptr,&op,1,step);require(function,"native instruction compilation");
        for(unsigned sample=0;sample<128;sample++) {
            for(auto& reg:actual->regs)reg=int64_t(random());actual->regs[0]=0;actual->hi=random();actual->lo=random();
            if(sample<64){actual->regs[1]=sample;actual->regs[2]=int64_t(UINT64_C(0x80000000ffffffff));}
            if(sample==64){actual->regs[1]=INT64_MIN;actual->regs[2]=INT64_MAX;}
            if(sample==65){actual->regs[1]=INT64_MAX;actual->regs[2]=INT64_MIN;}
            std::copy(std::begin(actual->regs),std::end(actual->regs),std::begin(expected->regs));
            expected->hi=actual->hi;expected->lo=actual->lo;reference(*expected,op);function(actual.get());++cases;
            if(std::memcmp(actual->regs,expected->regs,sizeof(actual->regs))||actual->hi!=expected->hi||actual->lo!=expected->lo) {
                std::fprintf(stderr,"Native CPU mismatch op=%08x sample=%u\n",op,sample);return 1;
            }
        }
    }
    uint32_t increment=(25u<<26)|(1<<21)|(1<<16)|1;
    n64emujit_config config={};config.link=link;config.chain_entry=&linked_body;
    auto function=n64emujit_compile_optimized(jit,nullptr,&increment,1,step,&config);require(function,"linked block compilation");
    actual->regs[1]=0;function(actual.get());require(actual->regs[1]==100000,"tail links must execute without growing the host stack");
    auto old=function;auto generation=n64emujit_generation(jit);
    for(unsigned i=0;n64emujit_entry_valid(jit,old,generation)&&i<100000;i++)require(n64emujit_compile(jit,nullptr,&increment,1,step),"arena recycling");
    require(!n64emujit_entry_valid(jit,old,generation),"recycled segment invalidates old entries");
    n64emujit_destroy(jit);std::printf("CPU JIT32: %u native arithmetic cases, 100000 tail links and arena recycling passed.\n",cases);
}
