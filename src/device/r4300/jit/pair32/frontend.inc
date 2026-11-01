// Paired-word host lowering. The precise frontend owns instruction ordering,
// cache timing, exceptions and delay slots, as on the x64/AArch64 backends.
// Generated entries use the same generation-validated, nonrecursive tail links.
// SPDX-License-Identifier: GPL-2.0-or-later
#include "jit32/assembler.hpp"
#include <algorithm>
#include <cstdlib>
#include <cstring>
extern "C" {
#include "../n64emujit.h"
#include "core/log.h"
#include "device/r4300/r4300_core.h"
#include "device/r4300/idec.h"
}
#ifdef _WIN32
#include <windows.h>
#else
#include <sys/mman.h>
#include <unistd.h>
#endif

#ifndef N64EMUJIT32_SEGMENT_BYTES
#define N64EMUJIT32_SEGMENT_BYTES (512*1024)
#endif
static constexpr size_t SEGMENT_BYTES=N64EMUJIT32_SEGMENT_BYTES,SEGMENTS=16,ARENA_BYTES=SEGMENT_BYTES*SEGMENTS;
struct n64emujit {
    uint8_t* code;
    size_t used,page;
    uint64_t generation,stamps[SEGMENTS],blocks,native,bytes,calls;
};
static int code_protect(void* p,size_t bytes,int executable) {
#ifdef _WIN32
    DWORD previous;return VirtualProtect(p,bytes,executable?PAGE_EXECUTE_READ:PAGE_READWRITE,&previous)!=0;
#else
    return !mprotect(p,bytes,executable?PROT_READ|PROT_EXEC:PROT_READ|PROT_WRITE);
#endif
}
#include "../code_write_scope.h"
n64emujit* n64emujit_create() {
    auto* jit=static_cast<n64emujit*>(std::calloc(1,sizeof(n64emujit)));if(!jit)return nullptr;
#ifdef _WIN32
    SYSTEM_INFO info;GetSystemInfo(&info);jit->page=info.dwPageSize;
    jit->code=static_cast<uint8_t*>(VirtualAlloc(nullptr,ARENA_BYTES,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE));
#else
    long page=sysconf(_SC_PAGESIZE);jit->page=page>0?size_t(page):4096;
    jit->code=static_cast<uint8_t*>(mmap(nullptr,ARENA_BYTES,PROT_READ|PROT_WRITE,MAP_PRIVATE|MAP_ANONYMOUS,-1,0));
    if(jit->code==MAP_FAILED)jit->code=nullptr;
#endif
    if(!jit->code){std::free(jit);return nullptr;}jit->generation=jit->stamps[0]=1;return jit;
}
void n64emujit_destroy(n64emujit* jit) {
    if(!jit)return;
#ifdef _WIN32
    VirtualFree(jit->code,0,MEM_RELEASE);
#else
    munmap(jit->code,ARENA_BYTES);
#endif
    std::free(jit);
}
void* n64emujit_fault_guard(const n64emujit*){return nullptr;}
void* n64emujit_fault_enter(n64emujit*){return nullptr;}
void n64emujit_fault_leave(void*){}
uint64_t n64emujit_fault_count(const n64emujit*){return 0;}
void n64emujit_fault_resolve(n64emujit*,r4300_core*,uint32_t){}
uint64_t n64emujit_generation(const n64emujit* jit){return jit->generation;}
const uint64_t* n64emujit_entry_generation_ptr(const n64emujit* jit,n64emujit_entry entry) {
    uintptr_t offset=uintptr_t(entry)-uintptr_t(jit?jit->code:nullptr);
    return jit&&entry&&offset<ARENA_BYTES?&jit->stamps[offset/SEGMENT_BYTES]:nullptr;
}
int n64emujit_entry_valid(const n64emujit* jit,n64emujit_entry entry,uint64_t generation) {
    const auto* stamp=n64emujit_entry_generation_ptr(jit,entry);return stamp&&generation&&*stamp==generation;
}
void n64emujit_stats(const n64emujit* jit,uint64_t* blocks,uint64_t* native) {
    *blocks=jit?jit->blocks:0;*native=jit?jit->native:0;
}
void n64emujit_code_stats(const n64emujit* jit,uint64_t stats[3]) {
    stats[0]=jit?jit->bytes:0;stats[1]=0;stats[2]=jit?jit->calls:0;
}

namespace {
using namespace n64emu32;
class Compiler {
public:
    Assembler host;
    uint64_t calls=0,native=0;
    static constexpr unsigned STATE=Assembler::STATE,FP=Assembler::FP;
    static int32_t gpr(unsigned n){return int32_t(offsetof(r4300_core,regs)+n*8);}
    void load(unsigned reg,unsigned low=0,unsigned high=2) {
        if(reg){host.load(low,STATE,gpr(reg));host.load(high,STATE,gpr(reg)+4);}
        else {host.imm(low,0);host.imm(high,0);}
    }
    void store(unsigned reg) { if(reg){host.store(0,STATE,gpr(reg));host.store(2,STATE,gpr(reg)+4);} }
    void sign_word() {host.mov(2,0);host.shift(2,ASR,31);}
    void constant(uint64_t value) {host.imm(0,uint32_t(value));host.imm(2,uint32_t(value>>32));}
    void step(void* block,unsigned index,unsigned phase,n64emujit_step callback,Label& exit) {
        // AAPCS and cdecl have different argument placement. Snapshot the state
        // pointer in its ABI slot before populating the remaining arguments.
        host.mov(0,STATE);host.argument(0,0);
        host.imm(0,uint32_t(uintptr_t(block)));host.argument(1,0);
        host.imm(0,index);host.argument(2,0);
        host.imm(0,phase);host.argument(3,0);
#ifdef N64EMU_JIT32_ARM
        host.mov(0,STATE);
#endif
        host.call(reinterpret_cast<const void*>(callback));++calls;
        host.immediate(CMP,0,0);host.branch(exit,EQ);
    }
    void compare(bool signed_compare) {
        Label yes,no,done;
        host.alu(CMP,2,3);host.branch(yes,signed_compare?LT:LO);host.branch(no,signed_compare?GT:HI);
        host.alu(CMP,0,1);host.branch(yes,LO);
        host.bind(no);host.imm(0,0);host.branch(done);host.bind(yes);host.imm(0,1);host.bind(done);host.imm(2,0);
    }
    void shift64(Shift kind) {
        Label done,large;
        host.immediate(AND,1,63);host.immediate(CMP,1,0);host.branch(done,EQ);
        host.immediate(CMP,1,32);host.branch(large,HS);host.store(1,FP,-68);
        host.imm(3,32);host.alu(SUB,3,1);host.store(3,FP,-72);
        if(kind==LSL) {
            host.shift_register(2,LSL,1);host.mov(3,0);
            host.load(1,FP,-72);host.shift_register(3,LSR,1);host.alu(OR,2,3);
            host.load(1,FP,-68);host.shift_register(0,LSL,1);
        } else {
            host.shift_register(0,LSR,1);host.mov(3,2);
            host.load(1,FP,-72);host.shift_register(3,LSL,1);host.alu(OR,0,3);
            host.load(1,FP,-68);host.shift_register(2,kind,1);
        }
        host.branch(done);host.bind(large);host.immediate(SUB,1,32);
        if(kind==LSL){host.mov(2,0);host.shift_register(2,LSL,1);host.imm(0,0);}
        else {host.mov(0,2);host.shift_register(0,kind,1);if(kind==ASR)host.shift(2,ASR,31);else host.imm(2,0);}
        host.bind(done);
    }
    bool instruction(uint32_t instruction);
};

static bool native_instruction(uint32_t instruction) {
    unsigned major=instruction>>26,fn=instruction&63;
    if(major)return major==9||major==10||major==11||major==12||major==13||major==14||major==15||major==25;
    switch(fn) {
    case 0:case 2:case 3:case 4:case 6:case 7:case 15:
    case 16:case 17:case 18:case 19:case 20:case 22:case 23:
    case 33:case 35:case 36:case 37:case 38:case 39:case 42:case 43:case 45:case 47:
    case 56:case 58:case 59:case 60:case 62:case 63:return true;
    default:return false;
    }
}
bool Compiler::instruction(uint32_t op) {
    if(!native_instruction(op))return false;
    unsigned major=op>>26,fn=op&63,rs=(op>>21)&31,rt=(op>>16)&31,rd=(op>>11)&31,amount=(op>>6)&31;
    if(!op||(!major&&fn==15))return true;
    if(!major&&(fn==16||fn==18)) {
        unsigned offset=fn==16?offsetof(r4300_core,hi):offsetof(r4300_core,lo);
        host.load(0,STATE,int32_t(offset));host.load(2,STATE,int32_t(offset+4));store(rd);return true;
    }
    if(!major&&(fn==17||fn==19)) {
        unsigned offset=fn==17?offsetof(r4300_core,hi):offsetof(r4300_core,lo);
        load(rs);host.store(0,STATE,int32_t(offset));host.store(2,STATE,int32_t(offset+4));return true;
    }
    bool word=false;
    if(!major&&(fn<=7||fn==20||fn==22||fn==23||fn>=56)) {
        bool variable=(fn>=4&&fn<=7)||(fn>=20&&fn<=23);
        if(variable){load(rs);host.store(0,FP,-60);}
        load(rt);if(variable)host.load(1,FP,-60);else host.imm(1,amount+(fn>=60?32:0));
        Shift kind=(fn&3)==0?LSL:(fn&3)==2?LSR:ASR;
        word=fn<=7;
        if(word)host.immediate(AND,1,31);
        if(word&&kind!=ASR)host.shift_register(0,kind,1);else shift64(kind);
    } else {
        load(rs);load(rt,1,3);
        if(major) {
            rd=rt;uint64_t value=major>=12&&major<=14?uint16_t(op):uint64_t(int64_t(int16_t(op)));
            host.imm(1,uint32_t(value));host.imm(3,uint32_t(value>>32));
            switch(major) {
            case 9:case 25:host.alu(ADD,0,1);host.alu(ADC,2,3);word=major==9;break;
            case 12:host.alu(AND,0,1);host.alu(AND,2,3);break;
            case 13:host.alu(OR,0,1);host.alu(OR,2,3);break;
            case 14:host.alu(XOR,0,1);host.alu(XOR,2,3);break;
            case 15:constant(uint64_t(int64_t(int32_t(op<<16))));break;
            case 10:case 11:compare(major==10);break;
            }
        } else switch(fn) {
        case 33:case 45:host.alu(ADD,0,1);host.alu(ADC,2,3);word=fn==33;break;
        case 35:case 47:host.alu(SUB,0,1);host.alu(SBC,2,3);word=fn==35;break;
        case 36:host.alu(AND,0,1);host.alu(AND,2,3);break;
        case 37:case 39:host.alu(OR,0,1);host.alu(OR,2,3);if(fn==39){host.immediate(XOR,0,UINT32_MAX);host.immediate(XOR,2,UINT32_MAX);}break;
        case 38:host.alu(XOR,0,1);host.alu(XOR,2,3);break;
        case 42:case 43:compare(fn==42);break;
        }
    }
    if(word)sign_word();store(rd);return true;
}

static n64emujit_entry compile(n64emujit* jit,void* block,const uint32_t* ops,unsigned count,
    n64emujit_step callback,const n64emujit_config* config) {
    if(!jit||!ops||!callback||!count||count>32)return nullptr;
    Compiler compiler;auto& host=compiler.host;host.frame();host.incoming_state();size_t chain=host.size();
    Label exit;
    for(unsigned i=0;i<count;i++) {
        if(native_instruction(ops[i])) {
            compiler.step(block,i,0,callback,exit);compiler.instruction(ops[i]);++compiler.native;
            compiler.step(block,i,1,callback,exit);
        } else compiler.step(block,i,2,callback,exit);
    }
    if(config&&config->link) {
        host.mov(0,Assembler::STATE);host.argument(0,0);host.call(reinterpret_cast<const void*>(config->link));++compiler.calls;
        host.immediate(CMP,0,0);host.branch(exit,EQ);host.jump_register(0);
    }
    host.bind(exit);host.leave();host.ret();
    size_t bytes=host.size(),reserve=(bytes+15)&~size_t(15);
    if(reserve>SEGMENT_BYTES)return nullptr;
    if(jit->used%SEGMENT_BYTES+reserve>SEGMENT_BYTES) {
        jit->used=((jit->used/SEGMENT_BYTES+1)*SEGMENT_BYTES)%ARENA_BYTES;
        jit->stamps[jit->used/SEGMENT_BYTES]=++jit->generation;
    }
    size_t page_start=jit->used&~(jit->page-1),page_end=(jit->used+reserve+jit->page-1)&~(jit->page-1);
    if(!code_protect(jit->code+page_start,page_end-page_start,0))return nullptr;
    code_write_scope publication(jit->code+page_start,page_end-page_start);
    auto* entry=jit->code+jit->used;std::memcpy(entry,host.data(),bytes);
#ifdef _WIN32
    FlushInstructionCache(GetCurrentProcess(),entry,bytes);
#else
    __builtin___clear_cache(reinterpret_cast<char*>(entry),reinterpret_cast<char*>(entry+bytes));
#endif
    if(!code_protect(jit->code+page_start,page_end-page_start,1))return nullptr;
    publication.published=true;jit->used+=reserve;++jit->blocks;jit->native+=compiler.native;jit->bytes+=bytes;jit->calls+=compiler.calls;
    if(config&&config->chain_entry)*config->chain_entry=reinterpret_cast<n64emujit_entry>(entry+chain);
    return reinterpret_cast<n64emujit_entry>(entry);
}
}
int n64emujit_has_native(const uint32_t* ops,unsigned count,uint32_t,unsigned,unsigned) {
    for(unsigned i=0;i<count;i++)if(native_instruction(ops[i]))return 1;return 0;
}
n64emujit_entry n64emujit_compile(n64emujit* jit,void* block,const uint32_t* ops,unsigned count,n64emujit_step step) {
    try{return compile(jit,block,ops,count,step,nullptr);}catch(const std::exception& error){core_log(CORE_LOG_ERROR,"32-bit JIT emission failed: %s",error.what());return nullptr;}
}
n64emujit_entry n64emujit_compile_optimized(n64emujit* jit,void* block,const uint32_t* ops,unsigned count,n64emujit_step step,const n64emujit_config* config) {
    try{return compile(jit,block,ops,count,step,config);}catch(const std::exception& error){core_log(CORE_LOG_ERROR,"32-bit JIT emission failed: %s",error.what());return nullptr;}
}
