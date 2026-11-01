// Generate A32/NEON bodies on the build host, with independent CEN64 results.
// This avoids emulating the C++ compiler/allocator while testing emitted code.
#include "device/rcp/rsp/p_rsp/jit_emitter.hpp"
#include "device/rcp/rsp/p_rsp/state.hpp"
#include "device/rcp/rsp/p_rsp/rsp_op.hpp"
#include <cstdio>
#include <cstring>
#include <random>
#include <vector>
#include <array>
using namespace RSP;
using Op=void (*)(CPUState*,unsigned,unsigned,unsigned,unsigned);
static const Op operations[64]={
 RSP_VMULF,RSP_VMULU,nullptr,nullptr,RSP_VMUDL,RSP_VMUDM,RSP_VMUDN,RSP_VMUDH,RSP_VMACF,RSP_VMACU,nullptr,nullptr,RSP_VMADL,RSP_VMADM,RSP_VMADN,RSP_VMADH,
 RSP_VADD,RSP_VSUB,nullptr,RSP_VABS,RSP_VADDC,RSP_VSUBC,nullptr,nullptr,nullptr,nullptr,nullptr,nullptr,nullptr,RSP_VSAR,nullptr,nullptr,
 RSP_VLT,RSP_VEQ,RSP_VNE,RSP_VGE,RSP_VCL,RSP_VCH,RSP_VCR,RSP_VMRG,RSP_VAND,RSP_VNAND,RSP_VOR,RSP_VNOR,RSP_VXOR,RSP_VNXOR,nullptr,nullptr,
 RSP_VRCP,RSP_VRCPL,RSP_VRCPH,RSP_VMOV,RSP_VRSQ,RSP_VRSQL,RSP_VRSQH,RSP_VNOP,nullptr,nullptr,nullptr,nullptr,nullptr,nullptr,nullptr,RSP_VNOP};
static FILE* output;
static void word(uint32_t n){std::fwrite(&n,4,1,output);}
static void table(const void* data,size_t bytes){word(uint32_t(uintptr_t(data)));word(uint32_t(bytes));std::fwrite(data,1,bytes,output);}
static void body(JIT::Emitter& emit,uint32_t tag,unsigned samples) {
    emit.movi(JIT::JIT_R0,0);emit.retr(JIT::JIT_R0);
    JIT::HostWord size;emit.get_code(&size);std::vector<uint8_t> code(size);emit.set_code(code.data(),size);emit.emit();
    word(tag);word(uint32_t(size));std::fwrite(code.data(),1,code.size(),output);word(samples);
}
int main(int argc,char** argv) {
    if(argc!=2)return 2;output=std::fopen(argv[1],"wb");if(!output)return 1;
    word(0x33505352);word(sizeof(CPUState));word(offsetof(CPUState,cp2));word(sizeof(CP2));word(offsetof(CPUState,sr)+4);word(3);
    JIT::Emitter::vector_tables(table);std::mt19937 random(0x41333256);
    for(unsigned op=0;op<64;op++)for(unsigned element=0;element<16;element++)for(unsigned alias=0;alias<8;alias++) {
        if(op==2||op==3||op==10||op==11)continue; // existing C helper, not a native SIMD lowering
        unsigned vs=(9+element+alias)&31,vt=(3+element*2+alias)&31;
        unsigned vd=alias==1?vs:alias==2?vt:alias==3?0:alias==4?31:5;
        if(alias==5)vt=vs;if(alias==6)vt=0;if(alias==7)vs=0;
        JIT::Emitter emit;emit.frame(256);emit.getarg(JIT::JIT_V0);
        emit.vector_instruction(op,vd,vs,vt,element);body(emit,(op<<24)|(element<<16)|(alias<<8),8);
        for(unsigned sample=0;sample<8;sample++) {
            CPUState actual={},expected={};auto* bytes=reinterpret_cast<uint8_t*>(&actual.cp2);
            for(unsigned i=0;i<sizeof(CP2);i++)bytes[i]=uint8_t(random());
            if(sample<4)for(auto& flag:actual.cp2.flags)for(auto& lane:flag.e)lane=(random()&1)?0xffff:0;
            if(sample<2)for(auto& v:actual.cp2.regs)for(auto& lane:v.e)lane=sample?0x8000:0;
            expected=actual;(operations[op]?operations[op]:RSP_RESERVED)(&expected,vd,vs,vt,element);
            std::fwrite(&actual,1,sizeof(actual),output);std::fwrite(&expected.cp2,1,sizeof(CP2),output);
        }
    }
    for(unsigned op:{0u,2u,4u,6u})for(unsigned rd=0;rd<32;rd++)for(unsigned element=0;element<16;element++) {
        JIT::Emitter emit;emit.frame(256);emit.getarg(JIT::JIT_V0);
        emit.ldxi_ui(JIT::JIT_R1,JIT::JIT_V0,offsetof(CPUState,sr)+4);
        emit.vector_transfer(op,JIT::JIT_R1,rd,element);
        emit.stxi_i(offsetof(CPUState,sr)+4,JIT::JIT_V0,JIT::JIT_R1);
        body(emit,0x40000000|(op<<24)|(element<<16)|(rd<<8),4);
        for(unsigned sample=0;sample<4;sample++) {
            CPUState actual={},expected={};for(unsigned i=0;i<sizeof(CP2);i++)reinterpret_cast<uint8_t*>(&actual.cp2)[i]=uint8_t(random());
            actual.sr[1]=random();expected=actual;
            if(op==0)RSP_MFC2(&expected,1,rd,element);if(op==2)RSP_CFC2(&expected,1,rd);
            if(op==4)RSP_MTC2(&expected,1,rd,element);if(op==6)RSP_CTC2(&expected,1,rd);
            std::fwrite(&actual,1,sizeof(actual),output);std::fwrite(&expected.cp2,1,sizeof(CP2),output);word(expected.sr[1]);
        }
    }
    using MemoryOp=void (*)(CPUState*,unsigned,unsigned,int,unsigned);
    const MemoryOp loads[]={RSP_LBV,RSP_LSV,RSP_LLV,RSP_LDV,RSP_LQV,RSP_LPV,RSP_LUV};
    const MemoryOp stores[]={RSP_SBV,RSP_SSV,RSP_SLV,RSP_SDV,RSP_SQV};
    for(unsigned type=0;type<7;type++)for(unsigned store=0;store<(type<5?2u:1u);store++)for(unsigned location=0;location<64;location++) {
        unsigned address=location<32?location:4064+location-32,bytes=type<5?1u<<type:8,vt=address&31;
        if(address&(bytes-1))continue;
        JIT::Emitter emit;emit.frame(256);emit.getarg(JIT::JIT_V0);emit.movi(JIT::JIT_V1,0x21000000);
        emit.ldxi_ui(JIT::JIT_R0,JIT::JIT_V0,offsetof(CPUState,sr)+4);
        unsigned offset=offsetof(CPUState,cp2.regs)+vt*16+(bytes==1?1:0);
        if(type>=5)emit.vector_packed_load(JIT::JIT_V0,JIT::JIT_V1,JIT::JIT_R0,offset,type==5?8:7);
        else emit.vector_memory(JIT::JIT_V0,JIT::JIT_V1,JIT::JIT_R0,offset,store,bytes);
        body(emit,0x80000000|(store<<24)|(type<<20)|address,4);
        for(unsigned sample=0;sample<4;sample++) {
            CPUState actual={},expected={};std::array<uint32_t,1024> memory,expected_memory;
            for(auto& value:memory)value=random();expected_memory=memory;
            for(unsigned i=0;i<sizeof(CP2);i++)reinterpret_cast<uint8_t*>(&actual.cp2)[i]=uint8_t(random());
            actual.sr[1]=address;expected=actual;expected.dmem=expected_memory.data();
            (store?stores[type]:loads[type])(&expected,vt,0,0,1);
            std::fwrite(&actual,1,sizeof(actual),output);std::fwrite(&expected.cp2,1,sizeof(CP2),output);
            std::fwrite(memory.data(),1,4096,output);std::fwrite(expected_memory.data(),1,4096,output);
        }
    }
    std::fclose(output);
}
