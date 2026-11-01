// 32-bit host lowering for the shared RSP compiler and vector SSA optimizer.
// Logical cache registers have frame homes, so three-source instructions and
// nested delay slots cannot exhaust IA-32's small scalar register file.
// SPDX-License-Identifier: GPL-2.0-or-later
#ifndef N64EMU_RSP_EMITTER32_HPP
#define N64EMU_RSP_EMITTER32_HPP
#include "jit32/assembler.hpp"
#include "simd_caps.hpp"
#include <array>
#include <memory>
#include <utility>
namespace RSP { namespace JIT {
using HostWord=int32_t;
using HostUWord=uint32_t;
using CodePointer=void*;
enum { JIT_R0=0,JIT_R1=1,JIT_V0=32,JIT_V1,JIT_V2,JIT_FP=63 };
constexpr unsigned JIT_R(unsigned n) { return n; }
constexpr unsigned JIT_V(unsigned n) { return JIT_V0+n; }
constexpr unsigned JIT_R_NUM=5,JIT_V_NUM=5;
struct CodeLabel { n64emu32::Label label;size_t offset=0; };
struct VectorProgram;
class Emitter {
    n64emu32::Assembler host;
    std::vector<std::unique_ptr<CodeLabel>> labels;
    struct Argument { bool immediate;HostWord value; };
    std::vector<Argument> arguments;
    uint8_t* destination=nullptr;
    bool optimize_code;
    static constexpr unsigned FP=n64emu32::Assembler::FP;
    static constexpr unsigned STATE=n64emu32::Assembler::STATE;
    static constexpr unsigned DMEM=n64emu32::Assembler::DMEM;
    static int32_t slot(unsigned n) { return -64-int32_t(n>=JIT_V0?n-JIT_V0+JIT_R_NUM:n)*4; }
    void loadv(unsigned n,unsigned physical) {
        if(n==JIT_V0)host.mov(physical,STATE);
        else if(n==JIT_V1)host.mov(physical,DMEM);
        else if(n==JIT_V2)host.mov(physical,n64emu32::Assembler::INDIRECT);
        else if(n==JIT_FP)host.mov(physical,FP);
        else host.load(physical,FP,slot(n));
    }
    void savev(unsigned n,unsigned physical) {
        if(n==JIT_V0)host.mov(STATE,physical);
        else if(n==JIT_V1)host.mov(DMEM,physical);
        else if(n==JIT_V2)host.mov(n64emu32::Assembler::INDIRECT,physical);
        else host.store(physical,FP,slot(n));
    }
    CodeLabel* new_label() { labels.emplace_back(new CodeLabel);return labels.back().get(); }
    void binary(unsigned d,unsigned a,unsigned b,n64emu32::Operation op) {
        loadv(a,0);loadv(b,1);host.alu(op,0,1);savev(d,0);
    }
    void immediate(unsigned d,unsigned a,HostWord value,n64emu32::Operation op) {
        loadv(a,0);host.immediate(op,0,uint32_t(value));savev(d,0);
    }
    void compare(unsigned d,unsigned a,unsigned b,n64emu32::Condition cc) {
        loadv(a,0);loadv(b,1);host.alu(n64emu32::CMP,0,1);condition(d,cc);
    }
    void compare_immediate(unsigned d,unsigned a,HostWord value,n64emu32::Condition cc) {
        loadv(a,0);host.immediate(n64emu32::CMP,0,uint32_t(value));condition(d,cc);
    }
    void condition(unsigned d,n64emu32::Condition cc) {
        n64emu32::Label yes,done;host.branch(yes,cc);host.imm(0,0);host.branch(done);
        host.bind(yes);host.imm(0,1);host.bind(done);savev(d,0);
    }
    void shift(unsigned d,unsigned a,unsigned amount,n64emu32::Shift kind,bool variable) {
        loadv(a,0);
        if(variable){loadv(amount,1);host.immediate(n64emu32::AND,1,31);host.shift_register(0,kind,1);}
        else host.shift(0,kind,amount);
        savev(d,0);
    }
    void memory_load(unsigned d,unsigned b,HostWord offset,unsigned bytes,bool sign) {
        loadv(b,1);host.load(0,1,int32_t(offset),bytes,sign);savev(d,0);
    }
    void memory_index_load(unsigned d,unsigned b,unsigned index,unsigned bytes,bool sign) {
        loadv(b,0);loadv(index,1);host.alu(n64emu32::ADD,0,1);host.load(0,0,0,bytes,sign);savev(d,0);
    }
    void memory_store(unsigned b,unsigned s,HostWord offset,unsigned bytes) {
        loadv(s,0);loadv(b,1);host.store(0,1,int32_t(offset),bytes);
    }
    void memory_index_store(unsigned b,unsigned s,unsigned index,unsigned bytes) {
        loadv(b,1);loadv(index,2);host.alu(n64emu32::ADD,1,2);loadv(s,0);host.store(0,1,0,bytes);
    }
    void emit_vector_program(const VectorProgram&);
    void vector_scalar(unsigned,unsigned,unsigned,unsigned,unsigned);
    void simd_node(unsigned,unsigned,unsigned,unsigned,unsigned,uint32_t);
    void simd_spill(unsigned,unsigned);
    void simd_reload(unsigned,unsigned);
    void simd_memory(unsigned,unsigned,int32_t,bool,unsigned=16);
#ifdef N64EMU_JIT32_ARM
    void neon3(uint32_t example,unsigned d,unsigned a,unsigned b) {
        host.word((example&~0x004ff0afu)|((d&15)<<12)|((d&16)<<18)|((a&15)<<16)|((a&16)<<3)|(b&15)|((b&16)<<1));
    }
    void neon2(uint32_t example,unsigned d,unsigned a) {
        host.word((example&~0x0040f02fu)|((d&15)<<12)|((d&16)<<18)|(a&15)|((a&16)<<1));
    }
#endif
    void vector_stack(int bytes) { host.stack_adjust(bytes); }
public:
    explicit Emitter(bool optimize=true):optimize_code(optimize) {}
    void frame(unsigned) { host.frame(); }
    void getarg(unsigned d) { host.incoming_state();savev(d,STATE); }
    void retr(unsigned s) { loadv(s,0);host.leave();host.ret(); }
    void prepare() { arguments.clear(); }
    void pushargr(unsigned source) { arguments.push_back({false,HostWord(source)}); }
    void pushargi(HostWord value) { arguments.push_back({true,value}); }
    void finishi(void* function) {
        assert(arguments.size()<=8);
        // Snapshot all arguments before changing any ABI argument register.
        for(unsigned i=0;i<arguments.size();i++) {
            if(arguments[i].immediate)host.imm(0,uint32_t(arguments[i].value));else loadv(unsigned(arguments[i].value),0);
            host.store(0,FP,-512+int32_t(i*4));
        }
        for(unsigned i=0;i<arguments.size();i++) { host.load(0,FP,-512+int32_t(i*4));host.argument(i,0); }
#ifdef N64EMU_JIT32_ARM
        // Writing r1..r3 through r0 above also overwrites argument zero.
        if(!arguments.empty())host.load(0,FP,-512);
#endif
#ifdef N64EMU_JIT32_ARM
        host.store(14,FP,-940);
#endif
        host.call(function);savev(JIT_R0,0);
#ifdef N64EMU_JIT32_ARM
        host.load(14,FP,-940);
#endif
    }
    void retval(unsigned d) { movr(d,JIT_R0); }
    void movi(unsigned d,HostWord value) { host.imm(0,uint32_t(value));savev(d,0); }
    void movr(unsigned d,unsigned s) { if(d!=s){loadv(s,0);savev(d,0);} }
#define RSP32_BINARY(name,op) void name(unsigned d,unsigned a,unsigned b){binary(d,a,b,n64emu32::op);}
    RSP32_BINARY(addr,ADD) RSP32_BINARY(subr,SUB) RSP32_BINARY(andr,AND) RSP32_BINARY(orr,OR) RSP32_BINARY(xorr,XOR)
#undef RSP32_BINARY
#define RSP32_IMMEDIATE(name,op) void name(unsigned d,unsigned a,HostWord v){immediate(d,a,v,n64emu32::op);}
    RSP32_IMMEDIATE(addi,ADD) RSP32_IMMEDIATE(andi,AND) RSP32_IMMEDIATE(ori,OR) RSP32_IMMEDIATE(xori,XOR)
#undef RSP32_IMMEDIATE
#define RSP32_SHIFT(name,op,variable) void name(unsigned d,unsigned a,unsigned n){shift(d,a,n,n64emu32::op,variable);}
    RSP32_SHIFT(lshi,LSL,false) RSP32_SHIFT(rshi,ASR,false) RSP32_SHIFT(rshi_u,LSR,false)
    RSP32_SHIFT(lshr,LSL,true) RSP32_SHIFT(rshr,ASR,true) RSP32_SHIFT(rshr_u,LSR,true)
#undef RSP32_SHIFT
    void extr_i(unsigned d,unsigned a) { movr(d,a); }
    void extr_ui(unsigned d,unsigned a) { movr(d,a); }
#define RSP32_CMP(name,cc) void name(unsigned d,unsigned a,unsigned b){compare(d,a,b,n64emu32::cc);}
    RSP32_CMP(eqr,EQ) RSP32_CMP(ner,NE) RSP32_CMP(ltr,LT) RSP32_CMP(ltr_u,LO)
#undef RSP32_CMP
#define RSP32_CMP(name,cc) void name(unsigned d,unsigned a,HostWord v){compare_immediate(d,a,v,n64emu32::cc);}
    RSP32_CMP(lti,LT) RSP32_CMP(lti_u,LO) RSP32_CMP(gei,GE) RSP32_CMP(lei,LE) RSP32_CMP(gti,GT)
#undef RSP32_CMP
    CodeLabel* label(){auto* n=new_label();n->offset=host.size();host.bind(n->label);return n;}
    CodeLabel* indirect(){return label();}
    void patch(CodeLabel* n){n->offset=host.size();host.bind(n->label);}
    void patch_at(CodeLabel* n,CodeLabel* target){n->offset=target->offset;host.bind_at(n->label,target->offset);}
    CodeLabel* beqi(unsigned a,HostWord v){loadv(a,0);host.immediate(n64emu32::CMP,0,uint32_t(v));auto*n=new_label();host.branch(n->label,n64emu32::EQ);return n;}
    CodeLabel* bnei(unsigned a,HostWord v){loadv(a,0);host.immediate(n64emu32::CMP,0,uint32_t(v));auto*n=new_label();host.branch(n->label,n64emu32::NE);return n;}
    CodeLabel* bltr_u(unsigned a,unsigned b){loadv(a,0);loadv(b,1);host.alu(n64emu32::CMP,0,1);auto*n=new_label();host.branch(n->label,n64emu32::LO);return n;}
    CodeLabel* bmsi(unsigned a,HostWord v){loadv(a,0);host.immediate(n64emu32::TEST,0,uint32_t(v));auto*n=new_label();host.branch(n->label,n64emu32::NE);return n;}
    CodeLabel* jmpi(){auto*n=new_label();host.branch(n->label);return n;}
    void jmpr(unsigned a){loadv(a,0);host.jump_register(0);}
    void jump_absolute(void* target){host.jump(target);}
    void ldxi(unsigned d,unsigned b,HostWord off){memory_load(d,b,off,4,false);}
    void ldxi_i(unsigned d,unsigned b,HostWord off){memory_load(d,b,off,4,false);}
    void ldxi_ui(unsigned d,unsigned b,HostWord off){memory_load(d,b,off,4,false);}
#define RSP32_LOAD(name,bytes,sign) void name(unsigned d,unsigned b,unsigned i){memory_index_load(d,b,i,bytes,sign);}
    RSP32_LOAD(ldxr_c,1,true) RSP32_LOAD(ldxr_uc,1,false) RSP32_LOAD(ldxr_s,2,true)
    RSP32_LOAD(ldxr_us,2,false) RSP32_LOAD(ldxr_i,4,false) RSP32_LOAD(ldxr_ui,4,false)
#undef RSP32_LOAD
    void stxi(HostWord off,unsigned b,unsigned s){memory_store(b,s,off,4);}
    void stxi_i(HostWord off,unsigned b,unsigned s){memory_store(b,s,off,4);}
#define RSP32_STORE(name,bytes) void name(unsigned i,unsigned b,unsigned s){memory_index_store(b,s,i,bytes);}
    RSP32_STORE(stxr_c,1) RSP32_STORE(stxr_s,2) RSP32_STORE(stxr_i,4)
#undef RSP32_STORE
    void get_code(HostWord* bytes){*bytes=HostWord(host.size());}
    void set_code(void* p,HostWord){destination=static_cast<uint8_t*>(p);}
    void* emit(){std::memcpy(destination,host.data(),host.size());return destination;}
    void* address(CodeLabel* n){return destination+n->offset;}
    void native_vector_call(void* target){host.call(target);}
    void native_vector_return(){host.ret();}
    static bool native_quad_partial(){return false;}
    void vector_quad_partial(unsigned,unsigned,unsigned,HostWord,bool){assert(false);}
    void vector_quad_arguments(unsigned,unsigned,HostWord){assert(false);}
    void vector_quad_leaf(unsigned,bool){assert(false);}
    void vector_arguments(unsigned,unsigned,unsigned,unsigned);
    void vector_instruction(unsigned,unsigned,unsigned,unsigned,unsigned,bool=false);
    void vector_sequence(const uint32_t*,unsigned);
    void vector_transfer(unsigned,unsigned,unsigned,unsigned);
    void vector_memory(unsigned,unsigned,unsigned,HostWord,bool,unsigned);
    void vector_packed_load(unsigned,unsigned,unsigned,HostWord,unsigned);
#ifdef N64EMU_JIT32_CROSS_ARM
    static void vector_tables(void (*write)(const void*,size_t));
#endif
};
} }
#endif
