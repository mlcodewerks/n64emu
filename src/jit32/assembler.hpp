// Small A32/IA-32 encoding layer used by the 32-bit CPU and RSP backends.
// The block compilers own allocation, precise exits and code publication.
// SPDX-License-Identifier: GPL-2.0-or-later
#ifndef PARALLAX_JIT32_ASSEMBLER_HPP
#define PARALLAX_JIT32_ASSEMBLER_HPP
#include <cassert>
#include <cstdint>
#include <cstring>
#include <stdexcept>
#include <vector>
#if defined(__i386__) || defined(_M_IX86)
#define PARALLAX_JIT32_X86 1
#include <xbyak/xbyak.h>
#elif defined(__arm__) || defined(PARALLAX_JIT32_CROSS_ARM)
#define PARALLAX_JIT32_ARM 1
#else
#error The 32-bit assembler requires IA-32 or ARMv7.
#endif

namespace parallax32 {
enum Condition { EQ, NE, LO, HS, LT, GE, LE, GT, HI, LS, MI, PL, AL };
enum Operation { ADD, ADC, SUB, SBC, AND, OR, XOR, CMP, TEST };
enum Shift { LSL, LSR, ASR };
struct Label { size_t offset = size_t(-1); std::vector<size_t> sites; };
class Assembler {
#ifdef PARALLAX_JIT32_X86
    struct ScratchAllocator : Xbyak::Allocator { bool useProtect() const override { return false; } } allocator;
    std::vector<uint8_t> buffer = std::vector<uint8_t>(1024 * 1024);
public:
    Xbyak::CodeGenerator code{buffer.size(),buffer.data(),&allocator};
    static constexpr unsigned FP=5, STATE=6, DMEM=7, INDIRECT=3, TEMP=0, COUNT=1, AUX=2;
    static Xbyak::Reg32 reg(unsigned n) { return Xbyak::Reg32(n); }
    static Xbyak::Address mem(unsigned bits,unsigned base,int32_t offset) {
        return Xbyak::AddressFrame(bits)[reg(base)+offset];
    }
    size_t size() const { return code.getSize(); }
    const uint8_t* data() const { return code.getCode(); }
    void word(uint32_t v) { code.dd(v); }
    void imm(unsigned d,uint32_t v) { code.mov(reg(d),v); }
    void mov(unsigned d,unsigned s) { if(d!=s)code.mov(reg(d),reg(s)); }
    void alu(Operation op,unsigned d,unsigned s) {
        switch(op) {
        case ADD:code.add(reg(d),reg(s));break;case ADC:code.adc(reg(d),reg(s));break;
        case SUB:code.sub(reg(d),reg(s));break;case SBC:code.sbb(reg(d),reg(s));break;
        case AND:code.and_(reg(d),reg(s));break;case OR:code.or_(reg(d),reg(s));break;
        case XOR:code.xor_(reg(d),reg(s));break;case CMP:code.cmp(reg(d),reg(s));break;
        case TEST:code.test(reg(d),reg(s));break;
        }
    }
    void immediate(Operation op,unsigned d,uint32_t v) {
        switch(op) {
        case ADD:code.add(reg(d),v);break;case ADC:code.adc(reg(d),v);break;
        case SUB:code.sub(reg(d),v);break;case SBC:code.sbb(reg(d),v);break;
        case AND:code.and_(reg(d),v);break;case OR:code.or_(reg(d),v);break;
        case XOR:code.xor_(reg(d),v);break;case CMP:code.cmp(reg(d),v);break;
        case TEST:code.test(reg(d),v);break;
        }
    }
    void shift(unsigned d,Shift kind,unsigned amount) {
        if(!amount)return;
        if(kind==LSL)code.shl(reg(d),amount);else if(kind==LSR)code.shr(reg(d),amount);else code.sar(reg(d),amount);
    }
    void shift_register(unsigned d,Shift kind,unsigned count) {
        assert(count==1 && d!=1);
        if(kind==LSL)code.shl(reg(d),code.cl);else if(kind==LSR)code.shr(reg(d),code.cl);else code.sar(reg(d),code.cl);
    }
    void clz(unsigned d,unsigned s) { code.bsr(reg(d),reg(s));code.xor_(reg(d),31); }
    void load(unsigned d,unsigned base,int32_t off,unsigned bytes=4,bool sign=false) {
        if(bytes==4)code.mov(reg(d),mem(32,base,off));
        else if(sign)code.movsx(reg(d),mem(bytes*8,base,off));
        else code.movzx(reg(d),mem(bytes*8,base,off));
    }
    void store(unsigned s,unsigned base,int32_t off,unsigned bytes=4) {
        if(bytes==4)code.mov(mem(32,base,off),reg(s));
        else if(bytes==2)code.mov(mem(16,base,off),reg(s).cvt16());
        else { assert(s<4);code.mov(mem(8,base,off),reg(s).cvt8()); }
    }
    void call_register(unsigned r) { code.call(reg(r)); }
    void jump_register(unsigned r) { code.jmp(reg(r)); }
    void call(const void* target) { imm(0,uint32_t(uintptr_t(target)));call_register(0); }
    void jump(const void* target) { imm(0,uint32_t(uintptr_t(target)));jump_register(0); }
    void ret() { code.ret(); }
    void branch(Label& label,Condition condition=AL) {
        static const uint8_t cc[]={4,5,2,3,12,13,14,15,7,6,8,9};
        if(condition==AL)code.db(0xe9);else {code.db(0x0f);code.db(0x80+cc[condition]);}
        size_t site=size();code.dd(0);label.sites.push_back(site);
        if(label.offset!=size_t(-1))patch(label,site);
    }
    void patch(const Label& label,size_t site) {
        uint32_t delta=uint32_t(label.offset-site-4);std::memcpy(buffer.data()+site,&delta,4);
    }
    void frame() {
        code.push(code.ebp);code.mov(code.ebp,code.esp);
        code.push(code.ebx);code.push(code.esi);code.push(code.edi);
        code.and_(code.esp,-16);code.sub(code.esp,1024);
    }
    void leave() {
        code.lea(code.esp,code.ptr[code.ebp-12]);
        code.pop(code.edi);code.pop(code.esi);code.pop(code.ebx);code.pop(code.ebp);
    }
    void argument(unsigned n,unsigned source) { store(source,4,int32_t(n*4)); }
    void incoming_state() { load(STATE,FP,8); }
    void stack_adjust(int amount) { if(amount<0)code.sub(code.esp,-amount);else if(amount)code.add(code.esp,amount); }
#else
    std::vector<uint8_t> buffer;
    static unsigned condition_bits(Condition condition) {
        static const unsigned cc[]={0,1,3,2,11,10,13,12,8,9,4,5,14};return cc[condition];
    }
public:
    static constexpr unsigned FP=11, STATE=4, DMEM=5, INDIRECT=6, TEMP=0, COUNT=1, AUX=2;
    size_t size() const { return buffer.size(); }
    const uint8_t* data() const { return buffer.data(); }
    void word(uint32_t v) { size_t n=buffer.size();buffer.resize(n+4);std::memcpy(buffer.data()+n,&v,4); }
    void imm(unsigned d,uint32_t v) {
        word(0xe3000000u|((v&0xf000)<<4)|(d<<12)|(v&0xfff));
        if(v>>16){v>>=16;word(0xe3400000u|((v&0xf000)<<4)|(d<<12)|(v&0xfff));}
    }
    void mov(unsigned d,unsigned s) { if(d!=s)word(0xe1a00000u|(d<<12)|s); }
    void alu(Operation op,unsigned d,unsigned s) {
        static const unsigned codes[]={4,5,2,6,0,12,1,10,8};
        unsigned flags=(op==ADD||op==ADC||op==SUB||op==SBC||op==CMP||op==TEST)?1:0;
        word(0xe0000000u|(codes[op]<<21)|(flags<<20)|(d<<16)|((op==CMP||op==TEST?0:d)<<12)|s);
    }
    void immediate(Operation op,unsigned d,uint32_t v) {
        unsigned temporary=d==12?3:12;imm(temporary,v);alu(op,d,temporary);
    }
    void shift(unsigned d,Shift kind,unsigned amount) {
        if(!amount)return;
        assert(amount<=32);unsigned encoded=amount==32?0:amount;
        word(0xe1a00000u|(d<<12)|(encoded<<7)|(unsigned(kind)<<5)|d);
    }
    void shift_register(unsigned d,Shift kind,unsigned count) {
        word(0xe1a00010u|(d<<12)|(count<<8)|(unsigned(kind)<<5)|d);
    }
    void clz(unsigned d,unsigned s) { word(0xe16f0f10u|(d<<12)|s); }
    void memory(bool write,unsigned r,unsigned base,int32_t off,unsigned bytes,bool sign) {
        int limit=bytes==2||sign?255:4095;
        if(off>limit||off< -limit) {
            unsigned scratch=(base==12||r==12)?3:12;
            imm(scratch,uint32_t(off));alu(ADD,scratch,base);base=scratch;off=0;
        }
        unsigned up=off>=0,offset=off>=0?unsigned(off):unsigned(-off);
        if(bytes==4 || (bytes==1&&!sign))
            word(0xe5000000u|(up<<23)|((bytes==1)<<22)|(!write<<20)|(base<<16)|(r<<12)|offset);
        else {
            unsigned type=bytes==2?(sign?3:1):2;
            word(0xe1400090u|(up<<23)|(!write<<20)|(base<<16)|(r<<12)|((offset&0xf0)<<4)|(type<<5)|(offset&15));
        }
    }
    void load(unsigned d,unsigned base,int32_t off,unsigned bytes=4,bool sign=false) { memory(false,d,base,off,bytes,sign); }
    void store(unsigned s,unsigned base,int32_t off,unsigned bytes=4) { memory(true,s,base,off,bytes,false); }
    void call_register(unsigned r) { word(0xe12fff30u|r); }
    void jump_register(unsigned r) { word(0xe12fff10u|r); }
    void call(const void* target) { imm(12,uint32_t(uintptr_t(target)));call_register(12); }
    void jump(const void* target) { imm(12,uint32_t(uintptr_t(target)));jump_register(12); }
    void ret() { jump_register(14); }
    void branch(Label& label,Condition condition=AL) {
        size_t site=size();word((condition_bits(condition)<<28)|0x0a000000u);label.sites.push_back(site);
        if(label.offset!=size_t(-1))patch(label,site);
    }
    void patch(const Label& label,size_t site) {
        int32_t delta=int32_t(label.offset)-int32_t(site)-8;
        if((delta&3)||delta< -33554432||delta>33554428)throw std::runtime_error("A32 branch out of range");
        uint32_t v;std::memcpy(&v,buffer.data()+site,4);v=(v&0xff000000)|(uint32_t(delta>>2)&0xffffff);std::memcpy(buffer.data()+site,&v,4);
    }
    void frame() {
        word(0xe92d4ff0u); // push {r4-r11,lr}
        word(0xe24dd004u);mov(FP,13);word(0xe24ddb01u); // 1024-byte frame, 8-byte aligned
    }
    void leave() { mov(13,FP);word(0xe28dd004u);word(0xe8bd4ff0u); }
    void argument(unsigned n,unsigned source) {
        if(n<4)mov(n,source);else store(source,13,int32_t((n-4)*4));
    }
    void incoming_state() { mov(STATE,0); }
    void stack_adjust(int amount) { if(amount)immediate(amount<0?SUB:ADD,13,uint32_t(amount<0?-amount:amount)); }
#endif
    void bind(Label& label) {
        if(label.offset!=size_t(-1))throw std::runtime_error("JIT label bound twice");
        label.offset=size();for(size_t site:label.sites)patch(label,site);
    }
    void bind_at(Label& label,size_t offset) {
        if(label.offset!=size_t(-1))throw std::runtime_error("JIT label bound twice");
        label.offset=offset;for(size_t site:label.sites)patch(label,site);
    }
};
}
#endif
