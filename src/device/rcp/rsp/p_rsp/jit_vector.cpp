// Native SIMD lowering of the shared CEN64 vector arithmetic algorithms.
// Algorithm copyrights and license are retained in arch/simd/rsp/*.h.
#include "jit_emitter.hpp"
#include "state.hpp"
#include "rsp/reciprocal.h"
#include <algorithm>
#include <array>
#include "rsp/quantize.hpp"
#include <climits>
#include <unordered_map>

namespace RSP { namespace JIT {
alignas(16) static const uint32_t blend_byte_sign_bits[4] = {
    0x80808080u, 0x80808080u, 0x80808080u, 0x80808080u
};
enum class VOp { Load, Store, Constant, Shuffle, Add, Sub, Adds, Subs, Addu, Subu,
    And, Or, Xor, AndNot, Eq, Gt, Min, Max, MulLo, MulHi, MulHiU, Shl, Shr, Sar, UnpackLo, UnpackHi, Pack, Blend };
static bool commutative(VOp op)
{
    switch(op) {
    case VOp::Add: case VOp::Adds: case VOp::Addu: case VOp::And: case VOp::Or:
    case VOp::Xor: case VOp::Eq: case VOp::Min: case VOp::Max:
    case VOp::MulLo: case VOp::MulHi: case VOp::MulHiU:return true;
    default:return false;
    }
}
struct VectorProgram {
    struct Node { VOp op; int a, b, c; uint32_t imm; };
    struct NodeHash {
        size_t operator()(const Node &n) const {
            size_t h=unsigned(n.op);
            for(uint32_t v:{uint32_t(n.a),uint32_t(n.b),uint32_t(n.c),n.imm})h=(h^v)*size_t(0x100000001b3ull);
            return h;
        }
    };
    struct NodeEqual {
        bool operator()(const Node &a,const Node &b) const
        { return a.op==b.op && a.a==b.a && a.b==b.b && a.c==b.c && a.imm==b.imm; }
    };
    struct Value { VectorProgram *program = nullptr; int id = -1; };
    std::vector<Node> nodes;
    bool register_arguments=false;
    int input_s=-1,input_t=-1,output=-1;
    Value add(VOp op, int a=-1, int b=-1, int c=-1, uint32_t imm=0)
    { nodes.push_back({op,a,b,c,imm}); return {this,int(nodes.size()-1)}; }
    Value load(unsigned offset) { return add(VOp::Load,-1,-1,-1,offset); }
    void store(unsigned offset, Value v) { add(VOp::Store,v.id,-1,-1,offset); }
    void optimize();
};

// Local value numbering operates on immutable SSA values. State loads are
// forwarded through disjoint 16-byte state slots. Earlier stores to a slot
// become dead once overwritten in the same straight-line vector sequence.
// Preloaded leaf arguments remain distinct despite identical placeholder
// offsets. Sequences never cross guest control flow or observable exits.
void VectorProgram::optimize()
{
    std::vector<Node> original;original.swap(nodes);
    std::vector<int> replacement(original.size());
    std::unordered_map<unsigned,int> memory;
    std::unordered_map<unsigned,int> stores;
    std::unordered_map<Node,int,NodeHash,NodeEqual> values;
    values.reserve(original.size());
    auto constant=[&](int id,uint32_t &value) {
        if(id<0 || nodes[id].op!=VOp::Constant)return false;
        value=nodes[id].imm;return true;
    };
    auto intern=[&](Node n) {
        auto found=values.find(n);
        if(found!=values.end())return found->second;
        int id=int(nodes.size());values.emplace(n,id);nodes.push_back(n);return id;
    };
    for(unsigned i=0;i<original.size();i++) {
        Node n=original[i];
        for(int *id:{&n.a,&n.b,&n.c})if(*id>=0)*id=replacement[*id];
        if(n.op==VOp::Store) {
            auto previous=stores.find(n.imm);
            if(previous!=stores.end())nodes[previous->second]={VOp::Constant,-1,-1,-1,0};
            stores[n.imm]=int(nodes.size());
            replacement[i]=int(nodes.size());nodes.push_back(n);
            memory[n.imm]=n.a;
            continue;
        }
        if(n.op==VOp::Load) {
            bool argument=register_arguments && (int(i)==input_s || int(i)==input_t);
            auto found=memory.find(n.imm);
            if(!argument && found!=memory.end())replacement[i]=found->second;
            else {
                replacement[i]=int(nodes.size());nodes.push_back(n);
                if(!argument)memory[n.imm]=replacement[i];
            }
            continue;
        }
        if(commutative(n.op) && n.a>n.b)std::swap(n.a,n.b);
        uint32_t a=0,b=0,c=0;
        bool ka=constant(n.a,a),kb=constant(n.b,b),kc=constant(n.c,c);
        int copy=-1;bool folded=false;uint32_t value=0;
        if(n.a>=0 && n.a==n.b) {
            switch(n.op) {
            case VOp::And:case VOp::Or:case VOp::Min:case VOp::Max:case VOp::Blend:copy=n.a;break;
            case VOp::Xor:case VOp::Sub:case VOp::Subs:case VOp::Subu:case VOp::AndNot:case VOp::Gt:folded=true;break;
            case VOp::Eq:value=~0u;folded=true;break;
            default:break;
            }
        }
        if(kb && b==0) {
            switch(n.op) {
            case VOp::Add:case VOp::Sub:case VOp::Adds:case VOp::Subs:case VOp::Addu:case VOp::Subu:
            case VOp::Or:case VOp::Xor:copy=n.a;break;
            case VOp::And:case VOp::AndNot:case VOp::MulLo:case VOp::MulHi:case VOp::MulHiU:folded=true;break;
            default:break;
            }
        }
        if(ka && a==0) {
            switch(n.op) {
            case VOp::Add:case VOp::Adds:case VOp::Addu:case VOp::Or:case VOp::Xor:case VOp::AndNot:copy=n.b;break;
            case VOp::And:case VOp::Subu:case VOp::MulLo:case VOp::MulHi:case VOp::MulHiU:folded=true;break;
            default:break;
            }
        }
        if(ka && a==~0u && n.op==VOp::And)copy=n.b;
        if(kb && b==~0u && n.op==VOp::And)copy=n.a;
        if((ka && a==~0u && n.op==VOp::Or)||(kb && b==~0u && n.op==VOp::Or)){folded=true;value=~0u;}
        if(ka && a==~0u && n.op==VOp::AndNot){folded=true;value=0;}
        if(n.op==VOp::Blend && kc && (c==0 || c==~0u))copy=c?n.b:n.a;
        if((n.op==VOp::Shl || n.op==VOp::Shr || n.op==VOp::Sar) && n.imm==0)copy=n.a;
        if(n.op==VOp::Shuffle && ka) {
            // Constants repeat a 32-bit pair. Every nonidentity RSP element
            // selection chooses either its even or its odd 16-bit lane.
            value=((a>>((n.imm&1)*16))&65535)*0x10001u;folded=true;
        }
        // Fold the repeated 32-bit constant pattern as two independent 16-bit
        // lanes. Unsigned intermediates preserve wrapping; saturation is exact.
        bool shift=n.op==VOp::Shl || n.op==VOp::Shr || n.op==VOp::Sar;
        if(ka && (kb || shift)) {
            uint32_t packed=0;bool valid=true;
            for(unsigned lane=0;lane<2;lane++) {
                uint32_t x=(a>>(lane*16))&65535,y=(b>>(lane*16))&65535,z=0;
                int sx=x>=32768?int(x)-65536:int(x),sy=y>=32768?int(y)-65536:int(y);
                auto sat=[](int v){return uint32_t(std::max(-32768,std::min(32767,v)))&65535;};
                switch(n.op) {
                case VOp::Add:z=x+y;break;case VOp::Sub:z=x-y;break;
                case VOp::Adds:z=sat(sx+sy);break;case VOp::Subs:z=sat(sx-sy);break;
                case VOp::Addu:z=std::min(65535u,x+y);break;case VOp::Subu:z=x>y?x-y:0;break;
                case VOp::And:z=x&y;break;case VOp::Or:z=x|y;break;case VOp::Xor:z=x^y;break;case VOp::AndNot:z=~x&y;break;
                case VOp::Eq:z=x==y?65535:0;break;case VOp::Gt:z=sx>sy?65535:0;break;
                case VOp::Min:z=uint32_t(std::min(sx,sy));break;case VOp::Max:z=uint32_t(std::max(sx,sy));break;
                case VOp::MulLo:z=x*y;break;case VOp::MulHi:z=uint32_t(sx*sy)>>16;break;case VOp::MulHiU:z=(x*y)>>16;break;
                case VOp::Shl:z=n.imm<16?x<<n.imm:0;break;case VOp::Shr:z=n.imm<16?x>>n.imm:0;break;
                case VOp::Sar:{unsigned amount=std::min(15u,n.imm);z=(x>>amount)|(sx<0?(65535u<<(16-amount)):0);break;}
                default:valid=false;break;
                }
                packed|=(z&65535)<<(lane*16);
            }
            if(valid){folded=true;value=packed;copy=-1;}
        }
        if(copy>=0)replacement[i]=copy;
        else {
            if(folded)n={VOp::Constant,-1,-1,-1,value};
            replacement[i]=intern(n);
        }
    }
    if(input_s>=0)input_s=replacement[input_s];
    if(input_t>=0)input_t=replacement[input_t];
    if(output>=0)output=replacement[output];
}
namespace VectorAlgorithms {
using Value = VectorProgram::Value;
static thread_local VectorProgram *current;
static Value binary(VOp op, Value a, Value b) { assert(a.program==b.program); return a.program->add(op,a.id,b.id); }
#define BIN(name,op) static Value name(Value a,Value b) { return binary(VOp::op,a,b); }
BIN(_mm_add_epi16,Add) BIN(_mm_sub_epi16,Sub) BIN(_mm_adds_epi16,Adds) BIN(_mm_subs_epi16,Subs)
BIN(_mm_adds_epu16,Addu) BIN(_mm_subs_epu16,Subu) BIN(_mm_and_si128,And) BIN(_mm_or_si128,Or)
BIN(_mm_xor_si128,Xor) BIN(_mm_andnot_si128,AndNot) BIN(_mm_cmpeq_epi16,Eq) BIN(_mm_cmpgt_epi16,Gt)
BIN(_mm_min_epi16,Min) BIN(_mm_max_epi16,Max) BIN(_mm_mullo_epi16,MulLo)
BIN(_mm_mulhi_epi16,MulHi) BIN(_mm_mulhi_epu16,MulHiU)
BIN(_mm_unpacklo_epi16,UnpackLo) BIN(_mm_unpackhi_epi16,UnpackHi) BIN(_mm_packs_epi32,Pack)
#undef BIN
static Value _mm_cmplt_epi16(Value a,Value b) { return binary(VOp::Gt,b,a); }
#undef _mm_slli_epi16
#undef _mm_srli_epi16
#undef _mm_srai_epi16
#define SHIFT(name,op) static Value name(Value a,int n) { return a.program->add(VOp::op,a.id,-1,-1,n); }
SHIFT(_mm_slli_epi16,Shl) SHIFT(_mm_srli_epi16,Shr) SHIFT(_mm_srai_epi16,Sar)
#undef SHIFT
static Value _mm_setzero_si128() { return current->add(VOp::Constant); }
static Value _mm_set1_epi32(int n) { return current->add(VOp::Constant,-1,-1,-1,uint32_t(n)); }
static Value _mm_blendv_epi8(Value a,Value b,Value c) { return a.program->add(VOp::Blend,a.id,b.id,c.id); }
#ifndef __SSE4_1__
static Value rsp_blendv_epi8_fallback(Value a,Value b,Value c) { return _mm_blendv_epi8(a,b,c); }
#endif

// Compile the same arithmetic against symbolic vectors. Intrinsics here build
// an SSA program; none of these functions is called by generated code.
#define __m128i Value
#include "arch/simd/rsp/clamp.h"
#include "arch/simd/rsp/vabs.h"
#include "arch/simd/rsp/vadd.h"
#include "arch/simd/rsp/vaddc.h"
#include "arch/simd/rsp/vand.h"
#include "arch/simd/rsp/vch.h"
#include "arch/simd/rsp/vcl.h"
#include "arch/simd/rsp/vcmp.h"
#include "arch/simd/rsp/vcr.h"
#include "arch/simd/rsp/vmac.h"
#include "arch/simd/rsp/vmrg.h"
#include "arch/simd/rsp/vmul.h"
#include "arch/simd/rsp/vmulh.h"
#include "arch/simd/rsp/vmull.h"
#include "arch/simd/rsp/vmulm.h"
#include "arch/simd/rsp/vmuln.h"
#include "arch/simd/rsp/vor.h"
#include "arch/simd/rsp/vsub.h"
#include "arch/simd/rsp/vsubc.h"
#include "arch/simd/rsp/vxor.h"
#undef __m128i
}
static constexpr unsigned vec_offset(unsigned v) { return offsetof(CPUState,cp2)+offsetof(CP2,regs)+v*16; }
static constexpr unsigned acc_offset(unsigned part) { return offsetof(CPUState,cp2)+offsetof(CP2,acc)+part*16; }
static constexpr unsigned flag_offset(unsigned flag,unsigned part) { return offsetof(CPUState,cp2)+offsetof(CP2,flags)+flag*32+part*16; }

static bool append_vector_instruction(VectorProgram &p,unsigned op,unsigned vd,unsigned vs,unsigned vt,unsigned e)
{
    using namespace VectorAlgorithms;
    struct Binding { VectorProgram *old; Binding(VectorProgram &p):old(current){current=&p;} ~Binding(){current=old;} } binding(p);
    auto load=[&](unsigned off){return p.load(off);};
    auto store=[&](unsigned off,Value v){p.store(off,v);};
    auto zero=_mm_setzero_si128();
    Value s=load(vec_offset(vs)), t=load(vec_offset(vt));
    p.input_s=s.id;p.input_t=t.id;
    if(e>1)t=p.add(VOp::Shuffle,t.id,-1,-1,e);
    Value lo=load(acc_offset(2)), md=load(acc_offset(1)), hi=load(acc_offset(0));
    Value ge,le,eq,sign,vce,result=zero;
    bool accumulator=true, multiply=false;
    auto clear_vco=[&](){store(flag_offset(RSP_VCO,0),zero);store(flag_offset(RSP_VCO,1),zero);};
    switch(op) {
    case 0x00: result=rsp_vmulf_vmulu<false>(s,t,zero,&lo,&md,&hi);multiply=true;break;
    case 0x01: result=rsp_vmulf_vmulu<true>(s,t,zero,&lo,&md,&hi);multiply=true;break;
    case 0x04: result=rsp_vmadl_vmudl<false>(s,t,zero,&lo,&md,&hi);multiply=true;break;
    case 0x05: result=rsp_vmadm_vmudm<false>(s,t,zero,&lo,&md,&hi);multiply=true;break;
    case 0x06: result=rsp_vmadn_vmudn<false>(s,t,zero,&lo,&md,&hi);multiply=true;break;
    case 0x07: result=rsp_vmadh_vmudh<false>(s,t,zero,&lo,&md,&hi);multiply=true;break;
    case 0x08: result=rsp_vmacf_vmacu<false>(s,t,zero,&lo,&md,&hi);multiply=true;break;
    case 0x09: result=rsp_vmacf_vmacu<true>(s,t,zero,&lo,&md,&hi);multiply=true;break;
    case 0x0c: result=rsp_vmadl_vmudl<true>(s,t,zero,&lo,&md,&hi);multiply=true;break;
    case 0x0d: result=rsp_vmadm_vmudm<true>(s,t,zero,&lo,&md,&hi);multiply=true;break;
    case 0x0e: result=rsp_vmadn_vmudn<true>(s,t,zero,&lo,&md,&hi);multiply=true;break;
    case 0x0f: result=rsp_vmadh_vmudh<true>(s,t,zero,&lo,&md,&hi);multiply=true;break;
    case 0x10: result=rsp_vadd(s,t,load(flag_offset(RSP_VCO,1)),&lo);clear_vco();break;
    case 0x11: result=rsp_vsub(s,t,load(flag_offset(RSP_VCO,1)),&lo);clear_vco();break;
    case 0x13: result=rsp_vabs(s,t,&lo);break;
    case 0x14: result=rsp_vaddc(s,t,zero,&sign);lo=result;store(flag_offset(RSP_VCO,0),zero);store(flag_offset(RSP_VCO,1),sign);break;
    case 0x15: result=rsp_vsubc(s,t,zero,&eq,&sign);lo=result;store(flag_offset(RSP_VCO,0),eq);store(flag_offset(RSP_VCO,1),sign);break;
    case 0x1d: result=e==8?hi:e==9?md:e==10?lo:zero;accumulator=false;break;
    case 0x20: case 0x21: case 0x22: case 0x23:
        eq=load(flag_offset(RSP_VCO,0));sign=load(flag_offset(RSP_VCO,1));
        if(op==0x20)result=rsp_vlt(s,t,zero,&le,eq,sign);
        if(op==0x21)result=rsp_veq(s,t,zero,&le,eq,sign);
        if(op==0x22)result=rsp_vne(s,t,zero,&le,eq,sign);
        if(op==0x23)result=rsp_vge(s,t,zero,&le,eq,sign);
        clear_vco();store(flag_offset(RSP_VCC,0),zero);store(flag_offset(RSP_VCC,1),le);lo=result;break;
    case 0x24:
        ge=load(flag_offset(RSP_VCC,0));le=load(flag_offset(RSP_VCC,1));eq=load(flag_offset(RSP_VCO,0));sign=load(flag_offset(RSP_VCO,1));vce=load(flag_offset(RSP_VCE,1));
        result=rsp_vcl(s,t,zero,&ge,&le,eq,sign,vce);
        clear_vco();store(flag_offset(RSP_VCE,1),zero);store(flag_offset(RSP_VCC,0),ge);store(flag_offset(RSP_VCC,1),le);lo=result;break;
    case 0x25:
        result=rsp_vch(s,t,zero,&ge,&le,&eq,&sign,&vce);
        store(flag_offset(RSP_VCO,0),eq);store(flag_offset(RSP_VCO,1),sign);store(flag_offset(RSP_VCC,0),ge);store(flag_offset(RSP_VCC,1),le);store(flag_offset(RSP_VCE,1),vce);lo=result;break;
    case 0x26:
        result=rsp_vcr(s,t,zero,&ge,&le);clear_vco();store(flag_offset(RSP_VCE,1),zero);store(flag_offset(RSP_VCC,0),ge);store(flag_offset(RSP_VCC,1),le);lo=result;break;
    case 0x27: result=rsp_vmrg(s,t,load(flag_offset(RSP_VCC,1)));clear_vco();lo=result;break;
    case 0x28: result=rsp_vand(s,t);lo=result;break;
    case 0x29: result=rsp_vnand(s,t);lo=result;break;
    case 0x2a: result=rsp_vor(s,t);lo=result;break;
    case 0x2b: result=rsp_vnor(s,t);lo=result;break;
    case 0x2c: result=rsp_vxor(s,t);lo=result;break;
    case 0x2d: result=rsp_vnxor(s,t);lo=result;break;
    case 0x30: case 0x31: case 0x32: case 0x33: case 0x34: case 0x35: case 0x36:
        store(acc_offset(2),t);return false;
    case 0x37:case 0x3f:return true;
    default:lo=_mm_add_epi16(s,t);break; // VZERO: sum in ACC_LO, zero VD.
    }
    if(accumulator)store(acc_offset(2),lo);
    if(multiply){store(acc_offset(1),md);store(acc_offset(0),hi);}
    store(vec_offset(vd),result);
    p.output=int(p.nodes.size()-1);
    return true;
}

void Emitter::vector_instruction(unsigned op,unsigned vd,unsigned vs,unsigned vt,unsigned e,bool register_arguments)
{
    if(quantize_opcode(op)) {
        assert(!register_arguments);
        prepare();pushargr(JIT_V0);pushargi(op|(vd<<6)|(vs<<11)|(vt<<16)|(e<<21));
        finishi(reinterpret_cast<CodePointer>(execute_quantize));
        return;
    }
    VectorProgram p;
    p.register_arguments=register_arguments;
    bool vector_only=append_vector_instruction(p,op,vd,vs,vt,e);
    emit_vector_program(p);
    if(!vector_only)vector_scalar(op,vd,vs,vt,e);
}

void Emitter::vector_sequence(const uint32_t *instructions,unsigned count)
{
    VectorProgram p;
    for(unsigned i=0;i<count;i++) {
        uint32_t instr=instructions[i];
        if(quantize_opcode(instr&63)) {
            emit_vector_program(p);p=VectorProgram();
            vector_instruction(instr&63,(instr>>6)&31,(instr>>11)&31,(instr>>16)&31,(instr>>21)&15);
            continue;
        }
        bool vector_only=append_vector_instruction(p,instr&63,(instr>>6)&31,(instr>>11)&31,(instr>>16)&31,(instr>>21)&15);
        assert(vector_only); (void)vector_only;
    }
    emit_vector_program(p);
}

// Element selection is identical to the interpreter, independently of host SIMD.
alignas(16) static const std::array<std::array<uint8_t,16>,16> element_masks=[](){
    std::array<std::array<uint8_t,16>,16> masks={};
    for(unsigned e=0;e<16;e++)for(unsigned lane=0;lane<8;lane++) {
        unsigned src=e<2?lane:e<4?(lane&~1u)+(e&1):e<8?(lane&4)+(e&3):e&7;
        masks[e][lane*2]=uint8_t(src*2);masks[e][lane*2+1]=uint8_t(src*2+1);
    }return masks;
}();

#ifdef N64EMU_JIT32_CROSS_ARM
void Emitter::vector_tables(void (*write)(const void*,size_t))
{
    write(element_masks.data(),sizeof(element_masks));
    write(blend_byte_sign_bits,sizeof(blend_byte_sign_bits));
    write(rsp_reciprocal_rom,sizeof(rsp_reciprocal_rom));
}
#endif

void Emitter::vector_arguments(unsigned vd,unsigned vs,unsigned vt,unsigned e)
{
#if defined(N64EMU_RSP_JIT32)
    // The 32-bit leaves specialize their loads/stores for this instruction.
    (void)vd;(void)vs;(void)vt;(void)e;
#elif defined(__aarch64__) || defined(_M_ARM64) || defined(RSP_JIT_EMITTER_AARCH64)
    using namespace Xbyak_aarch64;
    code.ldr(QReg(0),ptr(r(JIT_V0),vec_offset(vs)));code.ldr(QReg(1),ptr(r(JIT_V0),vec_offset(vt)));
    if(e>1){imm(16,uintptr_t(element_masks[e].data()));code.ldr(QReg(30),ptr(XReg(16)));code.tbl(VReg(1).b16,VReg(1).b16,1,VReg(30).b16);}
    code.add(XReg(17),r(JIT_V0),vec_offset(vd));
#else
    code.movdqu(code.xmm1,code.ptr[r(JIT_V0)+vec_offset(vs)]);code.movdqu(code.xmm2,code.ptr[r(JIT_V0)+vec_offset(vt)]);
    if(e>1)vector_shuffle(code.xmm2,code.xmm2,e);
    code.lea(code.rcx,code.ptr[r(JIT_V0)+vec_offset(vd)]);
#endif
}

void Emitter::emit_vector_program(const VectorProgram &program)
{
    VectorProgram optimized=program;
    if(optimize_code)optimized.optimize();
    const VectorProgram &p=optimized;
    const unsigned count=unsigned(p.nodes.size());
    std::vector<bool> live(count,false),spilled(count,false);
    std::vector<unsigned> uses(count,0);
    std::vector<int> physical(count,-1);
    auto mark=[&](auto &self,int i)->void {
        if(i<0||live[i])return;
        live[i]=true;const auto &n=p.nodes[i];self(self,n.a);self(self,n.b);self(self,n.c);
    };
    for(unsigned i=0;i<count;i++)if(p.nodes[i].op==VOp::Store)mark(mark,int(i));
    for(unsigned i=0;i<count;i++)if(live[i])for(int a:{p.nodes[i].a,p.nodes[i].b,p.nodes[i].c})if(a>=0)++uses[a];
#if defined(N64EMU_RSP_JIT32)
#ifdef N64EMU_JIT32_X86
    const unsigned registers[]={1,2,3,4,5,6,7};
#else
    const unsigned registers[]={0,1,2,3,8,9,10,11,12,13};
#endif
#elif defined(__aarch64__) || defined(_M_ARM64) || defined(RSP_JIT_EMITTER_AARCH64)
    using namespace Xbyak_aarch64;
    const unsigned registers[]={0,1,2,3,4,5,6,7,16,17,18,19,20,21,22,23,24,25,26,27,28,29};
    auto vr=[](unsigned i){return VReg(i);};
#else
    const unsigned registers[]={1,2,3,4,5,6,7,8,9,10,11,12,13,14,15};
    auto vr=[](unsigned i){return Xbyak::Xmm(i);};
#endif
    std::array<int,32> owner;owner.fill(-1);
    unsigned input_count=0;
    if(p.register_arguments) {
        unsigned index=0;
        for(int id:{p.input_s,p.input_t}) {
#if defined(__aarch64__) || defined(_M_ARM64) || defined(RSP_JIT_EMITTER_AARCH64)
            unsigned reg=index;
#else
            unsigned reg=index+1;
#endif
            if(live[id]){physical[id]=int(reg);owner[reg]=id;++input_count;}++index;
        }
    }
    auto reusable=[&](const VectorProgram::Node &n,const std::vector<unsigned> &remaining)->int {
        if(n.a<0 || n.op==VOp::Store)return -1;
#if defined(__aarch64__) || defined(_M_ARM64) || defined(RSP_JIT_EMITTER_AARCH64) || defined(N64EMU_JIT32_ARM)
        // These expansions write the destination before consuming all inputs.
        if(n.op==VOp::Pack||n.op==VOp::Blend)return -1;
#endif
        for(unsigned input=0;input<2;input++) {
            int candidate=input?n.b:n.a;if(candidate<0)continue;
            if(input && !optimize_code)continue;
#if !defined(__aarch64__) && !defined(_M_ARM64) && !defined(RSP_JIT_EMITTER_AARCH64) && !defined(N64EMU_JIT32_ARM)
            if(input && !commutative(n.op))continue;
#endif
            unsigned consumed=0;for(int id:{n.a,n.b,n.c})if(id==candidate)++consumed;
            if(remaining[candidate]==consumed)return int(input);
        }
        return -1;
    };
    auto remaining=uses;
    unsigned pressure=input_count,peak=input_count;
    for(unsigned i=0;i<count;i++)if(live[i]) {
        const auto &n=p.nodes[i];
        if(p.register_arguments && (int(i)==p.input_s || int(i)==p.input_t))continue;
        peak=std::max(peak,pressure+unsigned(n.op!=VOp::Store && reusable(n,remaining)<0));
        for(int id:{n.a,n.b,n.c})if(id>=0 && --remaining[id]==0)--pressure;
        if(n.op!=VOp::Store)++pressure;
    }
    // Recycle homes when SSA values die. Reserving one slot per IR node made
    // fused sequences grow the stack with program length rather than liveness
    // and could jump over Windows' stack guard page.
    unsigned spill_bytes=peak>sizeof(registers)/sizeof(registers[0])?peak*16:0;
    std::vector<unsigned> home(count,0),free_homes;
    unsigned next_home=0;
    // Scratch homes are private to this instruction and never overlap the
    // shared branch-delay frame or the entry thunk's preserved SIMD registers.
#if defined(N64EMU_RSP_JIT32)
    if(spill_bytes)vector_stack(-int(spill_bytes));
    auto spill=[&](unsigned reg,unsigned id){simd_spill(reg,home[id]*16);};
    auto reload=[&](unsigned reg,unsigned id){simd_reload(reg,home[id]*16);};
#elif defined(__aarch64__) || defined(_M_ARM64) || defined(RSP_JIT_EMITTER_AARCH64)
    if(spill_bytes) {
        if(spill_bytes<4096)code.sub(code.sp,code.sp,spill_bytes);
        else { imm(16,spill_bytes);code.sub(code.sp,code.sp,XReg(16)); }
    }
    auto spill=[&](unsigned reg,unsigned id){code.str(QReg(reg),ptr(code.sp,uint32_t(home[id]*16)));};
    auto reload=[&](unsigned reg,unsigned id){code.ldr(QReg(reg),ptr(code.sp,uint32_t(home[id]*16)));};
#else
    if(spill_bytes)code.sub(code.rsp,spill_bytes);
    auto spill=[&](unsigned reg,unsigned id){code.movdqu(code.ptr[code.rsp+home[id]*16],vr(reg));};
    auto reload=[&](unsigned reg,unsigned id){code.movdqu(vr(reg),code.ptr[code.rsp+home[id]*16]);};
#endif
    for(unsigned i=0;i<count;i++)if(live[i]) {
        if(p.register_arguments && (int(i)==p.input_s || int(i)==p.input_t))continue;
        const auto &n=p.nodes[i];std::array<bool,32> locked={};
        auto allocate=[&](){
            for(unsigned reg:registers)if(owner[reg]<0)return reg;
            unsigned best=UINT_MAX,distance=0;
            for(unsigned reg:registers)if(!locked[reg]) {
                unsigned next=count;
                for(unsigned j=i+1;j<count;j++)if(live[j]&&(p.nodes[j].a==owner[reg]||p.nodes[j].b==owner[reg]||p.nodes[j].c==owner[reg])){next=j;break;}
                if(best==UINT_MAX||next>distance){best=reg;distance=next;}
            }
            assert(best!=UINT_MAX);unsigned id=unsigned(owner[best]);
            if(!spilled[id]){
                if(free_homes.empty())home[id]=next_home++;
                else {home[id]=free_homes.back();free_homes.pop_back();}
                assert((home[id]+1)*16<=spill_bytes);
                spill(best,id);spilled[id]=true;
            }physical[id]=-1;owner[best]=-1;return best;
        };
        unsigned inputs[3]={0,0,0};int args[3]={n.a,n.b,n.c};
        for(unsigned j=0;j<3;j++)if(args[j]>=0) {
            unsigned id=unsigned(args[j]);
            if(physical[id]<0){unsigned reg=allocate();reload(reg,id);physical[id]=int(reg);owner[reg]=int(id);}
            inputs[j]=unsigned(physical[id]);locked[inputs[j]]=true;
        }
        int reuse=reusable(n,uses);
        unsigned d=n.op==VOp::Store?0:reuse>=0?inputs[reuse]:allocate(),a=inputs[0],b=inputs[1],c=inputs[2];
        // SSE's destructive two-operand form can keep either dying operand for
        // a commutative operation. NEON can also reuse the second source of a
        // noncommutative, single-write operation without changing its order.
        if(reuse==1 && commutative(n.op))std::swap(a,b);
#if defined(N64EMU_RSP_JIT32)
        simd_node(unsigned(n.op),d,a,b,c,n.imm);
#elif defined(__aarch64__) || defined(_M_ARM64) || defined(RSP_JIT_EMITTER_AARCH64)
        auto dd=vr(d),aa=vr(a),bb=vr(b);
        switch(n.op) {
        case VOp::Load:code.ldr(QReg(d),ptr(r(JIT_V0),n.imm));break;
        case VOp::Store:if(p.register_arguments && int(i)==p.output)code.str(QReg(a),ptr(XReg(17)));else code.str(QReg(a),ptr(r(JIT_V0),n.imm));break;
        case VOp::Constant:imm(16,n.imm);code.dup(dd.s4,WReg(16));break;
        case VOp::Shuffle:imm(16,uintptr_t(element_masks[n.imm].data()));code.ldr(QReg(30),ptr(XReg(16)));code.tbl(dd.b16,aa.b16,1,vr(30).b16);break;
#define SIMD(op,name) case VOp::op:code.name(dd.h8,aa.h8,bb.h8);break;
        SIMD(Add,add) SIMD(Sub,sub) SIMD(Adds,sqadd) SIMD(Subs,sqsub) SIMD(Addu,uqadd) SIMD(Subu,uqsub)
        SIMD(Eq,cmeq) SIMD(Gt,cmgt) SIMD(Min,smin) SIMD(Max,smax) SIMD(MulLo,mul)
#undef SIMD
#define SIMD(op,name) case VOp::op:code.name(dd.b16,aa.b16,bb.b16);break;
        SIMD(And,and_) SIMD(Or,orr) SIMD(Xor,eor)
#undef SIMD
        case VOp::AndNot:code.bic(dd.b16,bb.b16,aa.b16);break;
        case VOp::MulHi:case VOp::MulHiU:
            if(n.op==VOp::MulHi){code.smull(vr(30).s4,aa.h4,bb.h4);code.smull2(vr(31).s4,aa.h8,bb.h8);}
            else{code.umull(vr(30).s4,aa.h4,bb.h4);code.umull2(vr(31).s4,aa.h8,bb.h8);}
            code.shrn(dd.h4,vr(30).s4,16);code.shrn2(dd.h8,vr(31).s4,16);break;
        case VOp::Shl:code.shl(dd.h8,aa.h8,n.imm);break;
        case VOp::Shr:code.ushr(dd.h8,aa.h8,n.imm);break;
        case VOp::Sar:code.sshr(dd.h8,aa.h8,n.imm);break;
        case VOp::UnpackLo:code.zip1(dd.h8,aa.h8,bb.h8);break;
        case VOp::UnpackHi:code.zip2(dd.h8,aa.h8,bb.h8);break;
        case VOp::Pack:code.sqxtn(dd.h4,aa.s4);code.sqxtn2(dd.h8,bb.s4);break;
        case VOp::Blend:code.sshr(vr(30).b16,vr(c).b16,7);code.mov(dd.b16,vr(30).b16);code.bsl(dd.b16,bb.b16,aa.b16);break;
        }
#else
        auto dd=vr(d),aa=vr(a),bb=vr(b);
        if(n.a>=0 && n.op!=VOp::Store && n.op!=VOp::Shuffle && d!=a)code.movdqa(dd,aa);
        switch(n.op) {
        case VOp::Load:code.movdqu(dd,code.ptr[r(JIT_V0)+n.imm]);break;
        case VOp::Store:if(p.register_arguments && int(i)==p.output)code.movdqu(code.ptr[code.rcx],aa);else code.movdqu(code.ptr[r(JIT_V0)+n.imm],aa);break;
        case VOp::Constant:if(!n.imm)code.pxor(dd,dd);else if(n.imm==UINT32_MAX)code.pcmpeqd(dd,dd);else{code.mov(code.r11d,n.imm);code.movd(dd,code.r11d);code.pshufd(dd,dd,0);}break;
        case VOp::Shuffle:vector_shuffle(dd,aa,n.imm);break;
#define SIMD(op,name) case VOp::op:code.name(dd,bb);break;
        SIMD(Add,paddw) SIMD(Sub,psubw) SIMD(Adds,paddsw) SIMD(Subs,psubsw) SIMD(Addu,paddusw) SIMD(Subu,psubusw)
        SIMD(And,pand) SIMD(Or,por) SIMD(Xor,pxor) SIMD(AndNot,pandn) SIMD(Eq,pcmpeqw) SIMD(Gt,pcmpgtw)
        SIMD(Min,pminsw) SIMD(Max,pmaxsw) SIMD(MulLo,pmullw) SIMD(MulHi,pmulhw) SIMD(MulHiU,pmulhuw)
        SIMD(UnpackLo,punpcklwd) SIMD(UnpackHi,punpckhwd) SIMD(Pack,packssdw)
#undef SIMD
        case VOp::Shl:code.psllw(dd,n.imm);break;
        case VOp::Shr:code.psrlw(dd,n.imm);break;
        case VOp::Sar:code.psraw(dd,n.imm);break;
        case VOp::Blend:
            code.movdqa(code.xmm0,vr(c));
            if (rsp_simd_level() >= 41) code.pblendvb(dd,bb);
            else {
                /* Expand each byte's sign bit, matching PBLENDVB even for
                 * arbitrary internal flag values. r11 and XMM0 are reserved
                 * temporaries; the remaining XMM registers may all be live. */
                code.mov(code.r11,reinterpret_cast<uintptr_t>(blend_byte_sign_bits));
                code.pminub(code.xmm0,code.ptr[code.r11]);
                code.pcmpeqb(code.xmm0,code.ptr[code.r11]);
                /* (~mask & (a ^ b)) ^ b. */
                code.pxor(dd,bb); code.pandn(code.xmm0,dd);
                code.movdqa(dd,code.xmm0); code.pxor(dd,bb);
            }
            break;
        }
#endif
        for(int id:args)if(id>=0 && --uses[id]==0) {
            if(physical[id]>=0){owner[unsigned(physical[id])]=-1;physical[id]=-1;}
            if(spilled[id])free_homes.push_back(home[id]);
        }
        if(n.op!=VOp::Store){physical[i]=int(d);owner[d]=int(i);}
    }
#if defined(N64EMU_RSP_JIT32)
    if(spill_bytes)vector_stack(int(spill_bytes));
#elif defined(__aarch64__) || defined(_M_ARM64) || defined(RSP_JIT_EMITTER_AARCH64)
    if(spill_bytes) {
        if(spill_bytes<4096)code.add(code.sp,code.sp,spill_bytes);
        else { imm(16,spill_bytes);code.add(code.sp,code.sp,XReg(16)); }
    }
#else
    if(spill_bytes)code.add(code.rsp,spill_bytes);
#endif
}

void Emitter::vector_transfer(unsigned op,unsigned scalar,unsigned vector,unsigned element)
{
#ifdef N64EMU_RSP_JIT32
#include "jit_vector32_transfer.inc"
#else
    const unsigned offset=vec_offset(vector),flag=std::min(vector&3u,2u);
    alignas(16) static const uint16_t bits[8]={1,2,4,8,16,32,64,128};
#if defined(__aarch64__) || defined(_M_ARM64) || defined(RSP_JIT_EMITTER_AARCH64)
    using namespace Xbyak_aarch64;
    if(op==0) {
        if(!(element&1))code.ldrsh(r(scalar),ptr(r(JIT_V0),offset+element));
        else {code.ldrb(WReg(16),ptr(r(JIT_V0),offset+(element^1)));code.ldrb(WReg(17),ptr(r(JIT_V0),offset+((((element+1)&15)^1))));code.orr(WReg(16),WReg(17),WReg(16),LSL,8);code.sxth(r(scalar),WReg(16));}
    } else if(op==4) {
        if(!(element&1))code.strh(w(scalar),ptr(r(JIT_V0),offset+element));
        else {code.lsr(WReg(16),w(scalar),8);code.strb(WReg(16),ptr(r(JIT_V0),offset+(element^1)));if(element!=15)code.strb(w(scalar),ptr(r(JIT_V0),offset+((element+1)^1)));}
    } else if(op==2) {
        imm(17,uintptr_t(bits));code.ldr(QReg(31),ptr(XReg(17)));
        for(unsigned part=0;part<2;part++) {
            code.ldr(QReg(30),ptr(r(JIT_V0),flag_offset(flag,1-part)));code.ushr(VReg(30).h8,VReg(30).h8,15);code.mul(VReg(30).h8,VReg(30).h8,VReg(31).h8);code.addv(HReg(30),VReg(30).h8);code.umov(WReg(16+part),VReg(30).h[0]);
        }
        code.orr(WReg(16),WReg(16),WReg(17),LSL,8);code.sxth(r(scalar),WReg(16));
    } else {
        imm(16,uintptr_t(bits));code.ldr(QReg(31),ptr(XReg(16)));
        for(unsigned part=0;part<2;part++) {
            if(part && flag==2)code.eor(VReg(30).b16,VReg(30).b16,VReg(30).b16);
            else {code.dup(VReg(30).h8,w(scalar));if(part)code.ushr(VReg(30).h8,VReg(30).h8,8);code.and_(VReg(30).b16,VReg(30).b16,VReg(31).b16);code.cmeq(VReg(30).h8,VReg(30).h8,VReg(31).h8);}
            code.str(QReg(30),ptr(r(JIT_V0),flag_offset(flag,1-part)));
        }
    }
#else
    auto state=r(JIT_V0);
    if(op==0) {
        if(!(element&1))code.movsx(r(scalar),code.word[state+offset+element]);
        else {code.movzx(code.r11d,code.byte[state+offset+(element^1)]);code.shl(code.r11d,8);code.movzx(code.ecx,code.byte[state+offset+(((element+1)&15)^1)]);code.or_(code.r11d,code.ecx);code.movsx(r(scalar),code.r11w);}
    } else if(op==4) {
        if(!(element&1))code.mov(code.word[state+offset+element],r(scalar).cvt16());
        else {code.mov(code.r11d,r(scalar).cvt32());code.shr(code.r11d,8);code.mov(code.byte[state+offset+(element^1)],code.r11b);if(element!=15)code.mov(code.byte[state+offset+((element+1)^1)],r(scalar).cvt8());}
    } else if(op==2) {
        code.movdqu(code.xmm0,code.ptr[state+flag_offset(flag,1)]);code.packsswb(code.xmm0,code.ptr[state+flag_offset(flag,0)]);code.pmovmskb(code.r11d,code.xmm0);code.movsx(r(scalar),code.r11w);
    } else {
        code.mov(code.r11,uintptr_t(bits));code.movd(code.xmm0,r(scalar).cvt32());code.pshuflw(code.xmm0,code.xmm0,0);code.pshufd(code.xmm0,code.xmm0,0);code.movdqa(code.xmm1,code.xmm0);
        code.pand(code.xmm0,code.ptr[code.r11]);code.pcmpeqw(code.xmm0,code.ptr[code.r11]);code.movdqu(code.ptr[state+flag_offset(flag,1)],code.xmm0);
        if(flag==2)code.pxor(code.xmm1,code.xmm1);
        else {code.psrlw(code.xmm1,8);code.pand(code.xmm1,code.ptr[code.r11]);code.pcmpeqw(code.xmm1,code.ptr[code.r11]);}
        code.movdqu(code.ptr[state+flag_offset(flag,0)],code.xmm1);
    }
#endif
#endif
}

void Emitter::vector_scalar(unsigned op,unsigned vd,unsigned vs,unsigned vt,unsigned e)
{
#ifdef N64EMU_RSP_JIT32
#include "jit_vector32_scalar.inc"
#else
    unsigned lane=vs&7;
    unsigned selected=op!=0x33?e&7:e<2?lane:e<4?(lane&~1u)|(e&1):e<8?(lane&~3u)|(e&3):e&7;
    const unsigned source=vec_offset(vt)+selected*2,dest=vec_offset(vd)+lane*2;
    const unsigned div_in=offsetof(CPUState,cp2)+offsetof(CP2,div_in),div_out=offsetof(CPUState,cp2)+offsetof(CP2,div_out),dp=offsetof(CPUState,cp2)+offsetof(CP2,dp_flag);
#if defined(__aarch64__) || defined(_M_ARM64) || defined(RSP_JIT_EMITTER_AARCH64)
    using namespace Xbyak_aarch64;
    if(op==0x33){code.ldrh(WReg(16),ptr(r(JIT_V0),source));code.strh(WReg(16),ptr(r(JIT_V0),dest));return;}
    if(op==0x32||op==0x36){code.ldrh(WReg(16),ptr(r(JIT_V0),source));code.strh(WReg(16),ptr(r(JIT_V0),div_in));code.ldrh(WReg(16),ptr(r(JIT_V0),div_out));code.strh(WReg(16),ptr(r(JIT_V0),dest));imm(16,1);code.strb(WReg(16),ptr(r(JIT_V0),dp));return;}
    Label single,input_ready,no_adjust,normal,zero_data,special,finish;
    if(op==0x31||op==0x35){code.ldrb(WReg(1),ptr(r(JIT_V0),dp));code.tbz(WReg(1),0,single);code.ldrh(WReg(1),ptr(r(JIT_V0),div_in));code.lsl(WReg(1),WReg(1),16);code.ldrh(WReg(0),ptr(r(JIT_V0),source));code.orr(WReg(0),WReg(0),WReg(1));code.b(input_ready);}
    code.L(single);code.ldrsh(WReg(0),ptr(r(JIT_V0),source));code.L(input_ready);code.strb(code.wzr,ptr(r(JIT_V0),dp));
    code.asr(WReg(1),WReg(0),31);code.eor(WReg(2),WReg(0),WReg(1));imm(16,uint32_t(-32768));code.cmp(WReg(0),WReg(16));code.b(LE,no_adjust);code.sub(WReg(2),WReg(2),WReg(1));code.L(no_adjust);
    code.cbz(WReg(2),zero_data);code.cmp(WReg(0),WReg(16));code.b(EQ,special);
    code.clz(WReg(3),WReg(2));code.lslv(WReg(4),WReg(2),WReg(3));imm(16,0x7fc00000);code.and_(WReg(4),WReg(4),WReg(16));code.lsr(WReg(4),WReg(4),22);
    if(op==0x34||op==0x35){code.and_(WReg(5),WReg(3),1);code.orr(WReg(4),WReg(4),0x200);code.and_(WReg(4),WReg(4),0x3fe);code.orr(WReg(4),WReg(4),WReg(5));}
    imm(16,uintptr_t(rsp_reciprocal_rom));code.ldrh(WReg(2),ptr(XReg(16),XReg(4),LSL,1));code.orr(WReg(2),WReg(2),0x10000);code.lsl(WReg(2),WReg(2),14);imm(16,31);code.sub(WReg(3),WReg(16),WReg(3));
    if(op==0x34||op==0x35)code.lsr(WReg(3),WReg(3),1);
    code.lsrv(WReg(2),WReg(2),WReg(3));code.eor(WReg(2),WReg(2),WReg(1));code.b(finish);
    code.L(zero_data);imm(2,0x7fffffff);code.b(finish);code.L(special);imm(2,0xffff0000);code.L(finish);
    code.lsr(WReg(16),WReg(2),16);code.strh(WReg(16),ptr(r(JIT_V0),div_out));code.strh(WReg(2),ptr(r(JIT_V0),dest));
#else
    auto state=r(JIT_V0);
    if(op==0x33){code.movzx(code.r11d,code.word[state+source]);code.mov(code.word[state+dest],code.r11w);return;}
    if(op==0x32||op==0x36){code.movzx(code.r11d,code.word[state+source]);code.mov(code.word[state+div_in],code.r11w);code.movzx(code.r11d,code.word[state+div_out]);code.mov(code.word[state+dest],code.r11w);code.mov(code.byte[state+dp],1);return;}
    Xbyak::Label single,input_ready,no_adjust,zero_data,special,finish;
    if(op==0x31||op==0x35){code.test(code.byte[state+dp],1);code.jz(single,NativeCode::T_NEAR);code.movzx(code.edx,code.word[state+div_in]);code.shl(code.edx,16);code.movzx(code.eax,code.word[state+source]);code.or_(code.eax,code.edx);code.jmp(input_ready);}
    code.L(single);code.movsx(code.eax,code.word[state+source]);code.L(input_ready);code.mov(code.byte[state+dp],0);
    code.mov(code.edx,code.eax);code.sar(code.edx,31);code.mov(code.r8d,code.eax);code.xor_(code.r8d,code.edx);code.cmp(code.eax,-32768);code.jle(no_adjust);code.sub(code.r8d,code.edx);code.L(no_adjust);
    code.test(code.r8d,code.r8d);code.jz(zero_data,NativeCode::T_NEAR);code.cmp(code.eax,-32768);code.je(special,NativeCode::T_NEAR);
    code.bsr(code.ecx,code.r8d);code.xor_(code.ecx,31);code.mov(code.r9d,code.r8d);code.shl(code.r9d,code.cl);code.and_(code.r9d,0x7fc00000);code.shr(code.r9d,22);
    if(op==0x34||op==0x35){code.mov(code.r10d,code.ecx);code.and_(code.r10d,1);code.or_(code.r9d,0x200);code.and_(code.r9d,0x3fe);code.or_(code.r9d,code.r10d);}
    code.mov(code.r11,uintptr_t(rsp_reciprocal_rom));code.movzx(code.r8d,code.word[code.r11+code.r9*2]);code.or_(code.r8d,0x10000);code.shl(code.r8d,14);code.xor_(code.ecx,31);
    if(op==0x34||op==0x35)code.shr(code.ecx,1);
    code.shr(code.r8d,code.cl);code.xor_(code.r8d,code.edx);code.jmp(finish);
    code.L(zero_data);code.mov(code.r8d,0x7fffffff);code.jmp(finish);code.L(special);code.mov(code.r8d,0xffff0000);code.L(finish);
    code.mov(code.r11d,code.r8d);code.shr(code.r11d,16);code.mov(code.word[state+div_out],code.r11w);code.mov(code.word[state+dest],code.r8w);
#endif
#endif
}
#ifdef N64EMU_RSP_JIT32
#include "jit_vector32.inc"
#endif
} }
