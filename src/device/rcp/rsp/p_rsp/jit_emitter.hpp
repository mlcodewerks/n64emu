// Host emission for the RSP block compiler. The frontend owns register allocation,
// block boundaries and delay slots; this layer owns encodings and the host ABI.
#ifndef RSP_JIT_EMITTER_HPP
#define RSP_JIT_EMITTER_HPP
#include <cstdint>
#include <cstring>
#include <memory>
#include <vector>
#include <cassert>
#if defined(__aarch64__) || defined(_M_ARM64) || defined(RSP_JIT_EMITTER_AARCH64)
#include <xbyak_aarch64/xbyak_aarch64.h>
#elif defined(__x86_64__) || defined(_M_X64)
#include <xbyak/xbyak.h>
#else
#error The RSP Xbyak JIT requires an x64 or AArch64 host.
#endif

namespace RSP { namespace JIT {
using HostWord = intptr_t;
static_assert(sizeof(HostWord) == 8, "The RSP JIT requires a 64-bit host ABI.");
using HostUWord = uintptr_t;
using CodePointer = void *;
enum { JIT_R0 = 0, JIT_R1 = 1, JIT_V0 = 32, JIT_V1, JIT_V2, JIT_FP = 63 };
constexpr unsigned JIT_R(unsigned i) { return JIT_R0 + i; }
constexpr unsigned JIT_V(unsigned i) { return JIT_V0 + i; }
#if defined(__aarch64__) || defined(_M_ARM64) || defined(RSP_JIT_EMITTER_AARCH64)
constexpr unsigned JIT_R_NUM = 8, JIT_V_NUM = 10;
using NativeLabel = Xbyak_aarch64::Label;
using NativeCode = Xbyak_aarch64::CodeGenerator;
using NativeAllocator = Xbyak_aarch64::Allocator;
#else
constexpr unsigned JIT_R_NUM = 5;
#ifdef _WIN32
constexpr unsigned JIT_V_NUM = 7;
#else
constexpr unsigned JIT_V_NUM = 5;
#endif
using NativeLabel = Xbyak::Label;
using NativeCode = Xbyak::CodeGenerator;
using NativeAllocator = Xbyak::Allocator;
#endif
struct CodeLabel { NativeLabel label; size_t offset = 0; };

class Emitter
{
    // Blocks contain at most 128 RSP instructions. Fail on overflow rather than
    // growing a buffer whose label addresses could already have been recorded.
    struct ScratchAllocator : NativeAllocator { bool useProtect() const override { return false; } } scratch_allocator;
    std::unique_ptr<uint8_t[]> buffer{new uint8_t[256 * 1024]};
    NativeCode code{256 * 1024, buffer.get(), &scratch_allocator};
    std::vector<std::unique_ptr<CodeLabel>> labels;
    struct Argument { bool immediate; uint64_t value; };
    Argument arguments[8] = {};
    struct Relocation { size_t offset; uintptr_t target; };
    std::vector<Relocation> relocations;
    unsigned argument_count = 0;
    uint8_t *destination = nullptr;
    CodeLabel *new_label()
    {
        labels.emplace_back(new CodeLabel);
        return labels.back().get();
    }
public:
    CodeLabel *label()
    {
        auto *n = new_label(); n->offset = code.getSize(); code.L(n->label); return n;
    }
    CodeLabel *indirect() { return label(); }
    void patch(CodeLabel *n) { code.L(n->label); }
    void patch_at(CodeLabel *n, CodeLabel *target)
    {
        size_t end = code.getSize(); code.setSize(target->offset);
        code.L(n->label); code.setSize(end);
    }
    void get_code(HostWord *size) { *size = HostWord(code.getSize()); }
    void set_code(void *ptr, HostWord) { destination = static_cast<uint8_t *>(ptr); }
    void *emit()
    {
        code.ready(); std::memcpy(destination, code.getCode(), code.getSize());
        for (auto &reloc : relocations) {
            intptr_t delta = intptr_t(reloc.target) - intptr_t(destination + reloc.offset + 4);
            assert(delta >= INT32_MIN && delta <= INT32_MAX);
            int32_t displacement = int32_t(delta);
            std::memcpy(destination + reloc.offset, &displacement, sizeof(displacement));
        }
        return destination;
    }
    void *address(CodeLabel *n) { return destination + n->offset; }
    void prepare() { argument_count = 0; }
    void pushargr(unsigned s) { assert(argument_count < 8); arguments[argument_count++] = {false, uint64_t(r(s).getIdx())}; }
    void pushargi(HostWord v) { assert(argument_count < 8); arguments[argument_count++] = {true, uint64_t(v)}; }

private:
    // Resolve parallel ABI moves without overwriting an argument that has yet
    // to be copied. Only cycles require a scratch register; stack arguments
    // are written before changing any register arguments.
    void call_arguments(const unsigned *abi, unsigned count, unsigned temporary)
    {
        struct Move { unsigned target; Argument source; };
        Move pending[8];
        unsigned pending_count = 0;
        for (unsigned i = count; i < argument_count; i++) stack_argument(i - count, arguments[i]);
        for (unsigned i = 0; i < argument_count && i < count; i++)
            if (arguments[i].immediate || arguments[i].value != abi[i]) pending[pending_count++] = {abi[i], arguments[i]};
        while (pending_count) {
            unsigned index = 0;
            for (; index < pending_count; index++) {
                bool needed = false;
                for (unsigned i = 0; i < pending_count; i++) {
                    auto &move = pending[i];
                    if (!move.source.immediate && move.source.value == pending[index].target) needed = true;
                }
                if (!needed) break;
            }
            if (index == pending_count) {
                unsigned target = pending[0].target;
                physical_move(temporary, {false, target});
                for (unsigned i = 0; i < pending_count; i++) {
                    auto &move = pending[i];
                    if (!move.source.immediate && move.source.value == target) move.source.value = temporary;
                }
            } else {
                physical_move(pending[index].target, pending[index].source);
                pending[index] = pending[--pending_count];
            }
        }
    }

#if defined(__aarch64__) || defined(_M_ARM64) || defined(RSP_JIT_EMITTER_AARCH64)
private:
    static Xbyak_aarch64::XReg r(unsigned i)
    { return Xbyak_aarch64::XReg(i == JIT_FP ? 29 : i >= JIT_V0 ? 19 + i - JIT_V0 : i); }
    static Xbyak_aarch64::WReg w(unsigned i) { return Xbyak_aarch64::WReg(r(i).getIdx()); }
    void imm(unsigned reg, uint64_t value)
    {
        code.mov(Xbyak_aarch64::XReg(reg), value);
    }
    void physical_move(unsigned target, Argument source)
    {
        if (source.immediate) imm(target, source.value);
        else code.mov(Xbyak_aarch64::XReg(target), Xbyak_aarch64::XReg(unsigned(source.value)));
    }
    void stack_argument(unsigned, Argument) { assert(false && "RSP helpers use at most eight arguments"); }
    void memory(unsigned base, HostWord offset)
    {
        imm(16, offset); code.add(Xbyak_aarch64::XReg(17), r(base), Xbyak_aarch64::XReg(16));
    }
    void compare_imm(unsigned a, HostWord value)
    {
        if (value >= 0 && value < 4096) code.cmp(r(a), uint32_t(value));
        else { imm(16, value); code.cmp(r(a), Xbyak_aarch64::XReg(16)); }
    }
    void condition(unsigned d, Xbyak_aarch64::Cond cc) { code.cset(r(d), cc); }
    CodeLabel *branch(Xbyak_aarch64::Cond cc)
    { auto *n = new_label(); code.b(cc, n->label); return n; }
public:
    void frame(unsigned)
    {
        using namespace Xbyak_aarch64;
        code.stp(XReg(29), XReg(30), pre_ptr(code.sp, -16)); code.mov(XReg(29), code.sp);
        code.sub(code.sp, code.sp, 384);
        for (unsigned i = 0; i < 10; i += 2)
            code.stp(XReg(19+i), XReg(20+i), ptr(code.sp, int32_t(272 + i*8)));
    }
    void getarg(unsigned d) { movr(d, 0); }
    void retr(unsigned s)
    {
        using namespace Xbyak_aarch64;
        movr(0, s);
        for (unsigned i = 0; i < 10; i += 2)
            code.ldp(XReg(19+i), XReg(20+i), ptr(code.sp, int32_t(272 + i*8)));
        code.add(code.sp, code.sp, 384); code.ldp(XReg(29), XReg(30), post_ptr(code.sp, 16)); code.ret();
    }
    void finishi(void *ptr)
    {
        using namespace Xbyak_aarch64;
        static const unsigned abi[] = {0,1,2,3,4,5,6,7};
        call_arguments(abi, 8, 16);
        imm(16, reinterpret_cast<uintptr_t>(ptr)); code.blr(XReg(16));
    }
    void retval(unsigned d) { movr(d, 0); }
    void movi(unsigned d, HostWord v) { imm(r(d).getIdx(), v); }
    void movr(unsigned d, unsigned s) { if (d != s) code.mov(r(d), r(s)); }
#define RSP_BINARY(name, op) void name(unsigned d,unsigned a,unsigned b) { code.op(r(d),r(a),r(b)); }
    RSP_BINARY(addr, add) RSP_BINARY(subr, sub) RSP_BINARY(andr, and_) RSP_BINARY(orr, orr) RSP_BINARY(xorr, eor)
#undef RSP_BINARY
#define RSP_IMMEDIATE(name, op) void name(unsigned d,unsigned a,HostWord v) { imm(16,v); code.op(r(d),r(a),Xbyak_aarch64::XReg(16)); }
    RSP_IMMEDIATE(andi, and_) RSP_IMMEDIATE(ori, orr) RSP_IMMEDIATE(xori, eor)
#undef RSP_IMMEDIATE
    void addi(unsigned d,unsigned a,HostWord v)
    {
        if (v >= 0 && v < 4096) code.add(r(d),r(a),uint32_t(v));
        else if (v < 0 && v > -4096) code.sub(r(d),r(a),uint32_t(-v));
        else { imm(16,v); code.add(r(d),r(a),Xbyak_aarch64::XReg(16)); }
    }
#define RSP_SHIFT(name, op) void name(unsigned d,unsigned a,unsigned b) { code.op(r(d),r(a),r(b)); }
    RSP_SHIFT(lshr, lslv) RSP_SHIFT(rshr, asrv) RSP_SHIFT(rshr_u, lsrv)
#undef RSP_SHIFT
#define RSP_SHIFT(name, op) void name(unsigned d,unsigned a,unsigned n) { code.op(r(d),r(a),n); }
    RSP_SHIFT(lshi, lsl) RSP_SHIFT(rshi, asr) RSP_SHIFT(rshi_u, lsr)
#undef RSP_SHIFT
    void extr_i(unsigned d,unsigned a) { code.sxtw(r(d),w(a)); }
    void extr_ui(unsigned d,unsigned a) { code.mov(w(d),w(a)); }
#define RSP_CMP(name, cc) void name(unsigned d,unsigned a,unsigned b) { code.cmp(r(a),r(b)); condition(d,Xbyak_aarch64::cc); }
    RSP_CMP(eqr, EQ) RSP_CMP(ner, NE) RSP_CMP(ltr, LT) RSP_CMP(ltr_u, LO)
#undef RSP_CMP
#define RSP_CMP(name, cc) void name(unsigned d,unsigned a,HostWord v) { compare_imm(a,v); condition(d,Xbyak_aarch64::cc); }
    RSP_CMP(lti, LT) RSP_CMP(lti_u, LO) RSP_CMP(gei, GE) RSP_CMP(lei, LE) RSP_CMP(gti, GT)
#undef RSP_CMP
    CodeLabel *beqi(unsigned a,HostWord v) { compare_imm(a,v); return branch(Xbyak_aarch64::EQ); }
    CodeLabel *bnei(unsigned a,HostWord v) { compare_imm(a,v); return branch(Xbyak_aarch64::NE); }
    CodeLabel *bltr_u(unsigned a,unsigned b) { code.cmp(r(a),r(b)); return branch(Xbyak_aarch64::LO); }
    CodeLabel *bmsi(unsigned a,HostWord v) { imm(16,v); code.tst(r(a),Xbyak_aarch64::XReg(16)); return branch(Xbyak_aarch64::NE); }
    CodeLabel *jmpi() { auto *n = new_label(); code.b(n->label); return n; }
    void jmpr(unsigned a) { code.br(r(a)); }
    void jump_absolute(void *ptr) { imm(16,reinterpret_cast<uintptr_t>(ptr)); code.br(Xbyak_aarch64::XReg(16)); }
#define RSP_LOAD(name, op, unscaled, reg, size) \
    void name(unsigned d,unsigned b,HostWord off) { \
        if (off >= 0 && off < 4096*size && !(off & (size-1))) code.op(reg(d),Xbyak_aarch64::ptr(r(b),uint32_t(off))); \
        else if (off >= -256 && off < 256) code.unscaled(reg(d),Xbyak_aarch64::ptr(r(b),int32_t(off))); \
        else { memory(b,off); code.op(reg(d),Xbyak_aarch64::ptr(Xbyak_aarch64::XReg(17))); } }
    RSP_LOAD(ldxi, ldr, ldur, r, 8) RSP_LOAD(ldxi_i, ldrsw, ldursw, r, 4) RSP_LOAD(ldxi_ui, ldr, ldur, w, 4)
#undef RSP_LOAD
#define RSP_LOAD(name, op, reg) void name(unsigned d,unsigned b,unsigned i) { code.op(reg(d),Xbyak_aarch64::ptr(r(b),r(i))); }
    RSP_LOAD(ldxr_c, ldrsb, r) RSP_LOAD(ldxr_uc, ldrb, w) RSP_LOAD(ldxr_s, ldrsh, r)
    RSP_LOAD(ldxr_us, ldrh, w) RSP_LOAD(ldxr_i, ldrsw, r) RSP_LOAD(ldxr_ui, ldr, w)
#undef RSP_LOAD
#define RSP_STORE(name, reg, size) \
    void name(HostWord off,unsigned b,unsigned s) { \
        if (off >= 0 && off < 4096*size && !(off & (size-1))) code.str(reg(s),Xbyak_aarch64::ptr(r(b),uint32_t(off))); \
        else if (off >= -256 && off < 256) code.stur(reg(s),Xbyak_aarch64::ptr(r(b),int32_t(off))); \
        else { memory(b,off); code.str(reg(s),Xbyak_aarch64::ptr(Xbyak_aarch64::XReg(17))); } }
    RSP_STORE(stxi, r, 8) RSP_STORE(stxi_i, w, 4)
#undef RSP_STORE
#define RSP_STORE(name, op) void name(unsigned i,unsigned b,unsigned s) { code.op(w(s),Xbyak_aarch64::ptr(r(b),r(i))); }
    RSP_STORE(stxr_c, strb) RSP_STORE(stxr_s, strh) RSP_STORE(stxr_i, str)
#undef RSP_STORE
#else
private:
    static Xbyak::Reg64 r(unsigned i)
    {
        static const unsigned scratch[] = {0,2,8,9,10}; // rax, rdx, r8..r10
#ifdef _WIN32
        // Low-numbered bases avoid an extra SIB/displacement byte on the
        // state and DMEM accesses that dominate generated RSP blocks.
        static const unsigned saved[] = {3,7,6,12,13,14,15};
#else
        static const unsigned saved[] = {3,13,14,12,15};
#endif
        return Xbyak::Reg64(i == JIT_FP ? 5 : i >= JIT_V0 ? saved[i-JIT_V0] : scratch[i]);
    }
    void compare_imm(unsigned a,HostWord v)
    {
        if (v >= INT32_MIN && v <= INT32_MAX) code.cmp(r(a), int32_t(v));
        else { code.mov(code.r11,uint64_t(v)); code.cmp(r(a),code.r11); }
    }
    void physical_move(unsigned target, Argument source)
    {
        if (source.immediate) code.mov(Xbyak::Reg64(target), source.value);
        else code.mov(Xbyak::Reg64(target), Xbyak::Reg64(unsigned(source.value)));
    }
    void stack_argument(unsigned index, Argument source)
    {
        physical_move(11, source);
#ifdef _WIN32
        code.mov(code.qword[code.rsp + 32 + index*8], code.r11);
#else
        code.mov(code.qword[code.rsp + index*8], code.r11);
#endif
    }
    void condition(unsigned d,unsigned cc)
    {
        switch (cc) { case 0: code.sete(code.r11b); break; case 1: code.setne(code.r11b); break;
        case 2: code.setl(code.r11b); break; case 3: code.setb(code.r11b); break;
        case 4: code.setge(code.r11b); break; case 5: code.setle(code.r11b); break; case 6: code.setg(code.r11b); break; }
        code.movzx(r(d),code.r11b);
    }
public:
    void frame(unsigned)
    {
        code.push(code.rbp); code.mov(code.rbp,code.rsp);
        code.push(code.rbx); code.push(code.r12); code.push(code.r13); code.push(code.r14); code.push(code.r15);
#ifdef _WIN32
        code.push(code.rsi); code.push(code.rdi);
#endif
        code.sub(code.rsp,264); // aligned calls, shadow space and delay-slot spills
    }
    void getarg(unsigned d)
    {
#ifdef _WIN32
        code.mov(r(d),code.rcx);
#else
        code.mov(r(d),code.rdi);
#endif
    }
    void retr(unsigned s)
    {
        code.mov(code.rax,r(s)); code.add(code.rsp,264);
#ifdef _WIN32
        code.pop(code.rdi); code.pop(code.rsi);
#endif
        code.pop(code.r15); code.pop(code.r14); code.pop(code.r13); code.pop(code.r12); code.pop(code.rbx); code.pop(code.rbp); code.ret();
    }
    void finishi(void *ptr)
    {
#ifdef _WIN32
        static const unsigned args[] = {1,2,8,9};
        constexpr unsigned num_args = 4;
#else
        static const unsigned args[] = {7,6,2,1,8,9};
        constexpr unsigned num_args = 6;
#endif
        call_arguments(args, num_args, 11);
        code.mov(code.r11,reinterpret_cast<uintptr_t>(ptr)); code.call(code.r11);
    }
    void retval(unsigned d) { movr(d,JIT_R0); }
    void movi(unsigned d,HostWord v) { code.mov(r(d),uint64_t(v)); }
    void movr(unsigned d,unsigned a) { if (d != a) code.mov(r(d),r(a)); }
#define RSP_BINARY(name, op) void name(unsigned d,unsigned a,unsigned b) { if (d==b) code.op(r(d),r(a)); else { movr(d,a); code.op(r(d),r(b)); } }
    RSP_BINARY(addr, add) RSP_BINARY(andr, and_) RSP_BINARY(orr, or_) RSP_BINARY(xorr, xor_)
#undef RSP_BINARY
    void subr(unsigned d,unsigned a,unsigned b)
    {
        if (d != b || a == b) { movr(d,a); code.sub(r(d),r(b)); }
        else { code.mov(code.r11,r(a)); code.sub(code.r11,r(b)); code.mov(r(d),code.r11); }
    }
#define RSP_IMMEDIATE(name, op) void name(unsigned d,unsigned a,HostWord v) { movr(d,a); code.op(r(d),int32_t(v)); }
    RSP_IMMEDIATE(addi, add) RSP_IMMEDIATE(andi, and_) RSP_IMMEDIATE(ori, or_) RSP_IMMEDIATE(xori, xor_)
#undef RSP_IMMEDIATE
#define RSP_SHIFT(name, op) void name(unsigned d,unsigned a,unsigned b) { code.mov(code.rcx,r(b)); movr(d,a); code.op(r(d),code.cl); }
    RSP_SHIFT(lshr, shl) RSP_SHIFT(rshr, sar) RSP_SHIFT(rshr_u, shr)
#undef RSP_SHIFT
#define RSP_SHIFT(name, op) void name(unsigned d,unsigned a,unsigned n) { movr(d,a); if (n) code.op(r(d),n); }
    RSP_SHIFT(lshi, shl) RSP_SHIFT(rshi, sar) RSP_SHIFT(rshi_u, shr)
#undef RSP_SHIFT
    void extr_i(unsigned d,unsigned a) { code.movsxd(r(d),r(a).cvt32()); }
    void extr_ui(unsigned d,unsigned a) { code.mov(r(d).cvt32(),r(a).cvt32()); }
#define RSP_CMP(name, cc) void name(unsigned d,unsigned a,unsigned b) { code.cmp(r(a),r(b)); condition(d,cc); }
    RSP_CMP(eqr,0) RSP_CMP(ner,1) RSP_CMP(ltr,2) RSP_CMP(ltr_u,3)
#undef RSP_CMP
#define RSP_CMP(name, cc) void name(unsigned d,unsigned a,HostWord v) { compare_imm(a,v); condition(d,cc); }
    RSP_CMP(lti,2) RSP_CMP(lti_u,3) RSP_CMP(gei,4) RSP_CMP(lei,5) RSP_CMP(gti,6)
#undef RSP_CMP
    CodeLabel *beqi(unsigned a,HostWord v) { compare_imm(a,v); auto *n=new_label(); code.je(n->label,NativeCode::T_NEAR); return n; }
    CodeLabel *bnei(unsigned a,HostWord v) { compare_imm(a,v); auto *n=new_label(); code.jne(n->label,NativeCode::T_NEAR); return n; }
    CodeLabel *bltr_u(unsigned a,unsigned b) { code.cmp(r(a),r(b)); auto *n=new_label(); code.jb(n->label,NativeCode::T_NEAR); return n; }
    CodeLabel *bmsi(unsigned a,HostWord v) { code.test(r(a),int32_t(v)); auto *n=new_label(); code.jnz(n->label,NativeCode::T_NEAR); return n; }
    CodeLabel *jmpi() { auto *n=new_label(); code.jmp(n->label,NativeCode::T_NEAR); return n; }
    void jmpr(unsigned a) { code.jmp(r(a)); }
    void jump_absolute(void *ptr)
    {
        // Every block and thunk belongs to the same reserved 1 GiB arena.
        code.db(0xe9); relocations.push_back({code.getSize(), reinterpret_cast<uintptr_t>(ptr)}); code.dd(0);
    }
    void ldxi(unsigned d,unsigned b,HostWord off) { code.mov(r(d),code.qword[r(b)+off]); }
    void ldxi_i(unsigned d,unsigned b,HostWord off) { code.movsxd(r(d),code.dword[r(b)+off]); }
    void ldxi_ui(unsigned d,unsigned b,HostWord off) { code.mov(r(d).cvt32(),code.dword[r(b)+off]); }
#define RSP_LOAD(name, op, size) void name(unsigned d,unsigned b,unsigned i) { code.op(r(d),code.size[r(b)+r(i)]); }
    RSP_LOAD(ldxr_c,movsx,byte) RSP_LOAD(ldxr_s,movsx,word) RSP_LOAD(ldxr_i,movsxd,dword)
    RSP_LOAD(ldxr_uc,movzx,byte) RSP_LOAD(ldxr_us,movzx,word)
#undef RSP_LOAD
    void ldxr_ui(unsigned d,unsigned b,unsigned i) { code.mov(r(d).cvt32(),code.dword[r(b)+r(i)]); }
    void stxi(HostWord off,unsigned b,unsigned s) { code.mov(code.qword[r(b)+off],r(s)); }
    void stxi_i(HostWord off,unsigned b,unsigned s) { code.mov(code.dword[r(b)+off],r(s).cvt32()); }
    void stxr_c(unsigned i,unsigned b,unsigned s) { code.mov(code.byte[r(b)+r(i)],r(s).cvt8()); }
    void stxr_s(unsigned i,unsigned b,unsigned s) { code.mov(code.word[r(b)+r(i)],r(s).cvt16()); }
    void stxr_i(unsigned i,unsigned b,unsigned s) { code.mov(code.dword[r(b)+r(i)],r(s).cvt32()); }
#endif
};
} }
#endif
