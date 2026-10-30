#include "../fpu_flags.h"

static int native_fpu_instruction(uint32_t instruction)
{
    unsigned fmt = (instruction >> 21) & 31, fn = instruction & 63;
    if ((instruction >> 26) != 17) return 0;
    if (fmt == 16 || fmt == 17) return fn <= 15 || fn >= 48 || fn == 36 || fn == 37 || (fmt == 16 ? fn == 33 : fn == 32);
    return (fmt == 20 || fmt == 21) && (fn == 32 || fn == 33);
}
static int native_fpu_transfer(enum r4300_opcode op)
{
    return op == R4300_OP_MFC1 || op == R4300_OP_DMFC1 || op == R4300_OP_MTC1 || op == R4300_OP_DMTC1 || op == R4300_OP_CFC1;
}
static void emit_fpu_transfer(uint32_t instruction, enum r4300_opcode op)
{
    unsigned rt = (instruction >> 16) & 31, fs = (instruction >> 11) & 31;
    int wide = op == R4300_OP_DMFC1 || op == R4300_OP_DMTC1;
    if (op == R4300_OP_CFC1) {
        if (fs == 31 || fs == 0) emit_mem(0x63,1,RAX,R12,-1,0,fs == 31 ? offsetof(struct r4300_core,cp1.fcr31) : offsetof(struct r4300_core,cp1.fcr0));
        else emit_movimm(0,RAX);
        if (rt) emit_state_store_raw(gpr_offset(rt),RAX);
        return;
    }
    emit_state_load_raw((wide ? offsetof(struct r4300_core,cp1.regs_double) : offsetof(struct r4300_core,cp1.regs_simple)) + fs * sizeof(void*),RCX);
    if (op == R4300_OP_MTC1 || op == R4300_OP_DMTC1) {
        emit_state_load_raw(gpr_offset(rt),RAX); emit_mem(0x89,wide,RAX,RCX,-1,0,0);
    } else {
        emit_mem(wide ? 0x8b : 0x63,1,RAX,RCX,-1,0,0);
        if (rt) emit_state_store_raw(gpr_offset(rt),RAX);
    }
}
static void emit_mxcsr(unsigned ext,size_t offset) {
    const auto memory=host_mem(32,RSP,-1,0,offset);
    if(ext==3) JIT(stmxcsr(memory)); else { assert(ext==2); JIT(ldmxcsr(memory)); }
}
static void emit_xmm_bits(int integer,unsigned xmm,int load) {
    if(load) JIT(movq(Xbyak::Xmm(xmm),Xbyak::Reg64(integer)));
    else JIT(movq(Xbyak::Reg64(integer),Xbyak::Xmm(xmm)));
}

/* The shared prepare has validated inputs and charged fetch/issue hazards.
 * Commit ordinary results here; exceptional outputs, enabled exceptions and
 * event boundaries retain the shared finish without repeating arithmetic. */
static unsigned char* emit_fpu_fast_finish(uint32_t instruction, unsigned index,
    const struct parallaxjit_config* config, unsigned char** exits, unsigned* exit_count)
{
    unsigned fn = instruction & 63, fmt = (instruction >> 21) & 31;
    int integer = (fn >= 8 && fn <= 15) || fn == 36 || fn == 37;
    int single = fn == 32 || (fmt == 16 && fn != 33);
    unsigned char* slow[5]; unsigned slow_count = 0;
    if (fn < 48 && fn != 6) {
        emit_mov(R9,RAX); emit_imm_op(4,RAX,~UINT32_C(1),0);
        emit_reg_op(0x85,RAX,RAX,0); slow[slow_count++] = emit_jump(5);
        emit_mem_imm(0xf7,0,0,R12,-1,0,offsetof(struct r4300_core,cp1.fcr31),0x80);
        slow[slow_count++] = emit_jump(5);
        if (!integer) {
            emit_mov64(R8,RAX);
            if (single) emit_imm_op(4,RAX,0x7f800000,0);
            else { emit_shift_reg(RAX,5,52,1); emit_imm_op(4,RAX,2047,0); }
            emit_imm_op(7,RAX,single ? 0x7f800000 : 2047,0); slow[slow_count++] = emit_jump(4);
            emit_reg_op(0x85,RAX,RAX,0); unsigned char* normal = emit_jump(5);
            emit_mov64(R8,RAX); emit_shift_reg(RAX,4,single ? 41 : 12,1);
            emit_reg_op(0x85,RAX,RAX,1); slow[slow_count++] = emit_jump(5);
            patch_jump(normal,out);
        }
    }
    unsigned latency = fn >= 48 || fn == 6 ? 0 : r4300_base_cycles(instruction,CP0_STATUS_CU1) - 1;
    bool dynamic_latency=!config->compat_issue_clock && fn<48 && fn!=6;
    if(dynamic_latency)emit_mem(0x8b,0,R13,RSP,-1,0,52);
    emit_mem(0x8b,0,RAX,R12,-1,0,offsetof(struct r4300_core,cp0.count_phase));
    if(dynamic_latency)emit_reg_op(0x01,R13,RAX,0);else emit_addimm(RAX,latency,RAX);
    emit_shift_reg(RAX,5,1,0);
    emit_mem(0x8b,1,RDX,R12,-1,0,offsetof(struct r4300_core,cp0.cycle_count));
    emit_reg_op(0x01,RAX,RDX,1); emit_reg_op(0x85,RDX,RDX,1); slow[slow_count++] = emit_jump(9);
    if (fn >= 48) {
        emit_mem_imm(0x81,0,4,R12,-1,0,offsetof(struct r4300_core,cp1.fcr31),~UINT32_C(0x800000));
        emit_mov(R8,RAX); emit_shift_reg(RAX,4,23,0);
        emit_mem(0x09,0,RAX,R12,-1,0,offsetof(struct r4300_core,cp1.fcr31));
    } else {
        if (fn != 6) {
            emit_mov(R9,RAX); emit_shift_reg(RAX,4,12,0);
            emit_mov(R9,RDX); emit_shift_reg(RDX,4,2,0); emit_reg_op(0x09,RDX,RAX,0);
            emit_mem(0x09,0,RAX,R12,-1,0,offsetof(struct r4300_core,cp1.fcr31));
        }
        emit_mem(0x89,1,R8,R12,-1,0,offsetof(struct r4300_core,cp1.regs) + ((instruction >> 6) & 31) * sizeof(cp1_reg));
    }
    if(!dynamic_latency)emit_movimm(latency,R13);
    emit_mem_imm(0xc7,0,0,RSP,-1,0,8,0);
    emit_region_commit_native(config,index,1,0);
    emit_movimm64((intptr_t)&breakloop,R11);
    emit_mem_imm(0x80,0,7,R11,-1,0,0,0); exits[(*exit_count)++] = emit_jump(5);
    unsigned char* done = emit_b();
    for (unsigned n = 0; n < slow_count; ++n) patch_jump(slow[n],out);
    return done;
}
static constexpr size_t fpu_fetch_flag =
#ifdef _WIN32
    64; // Keep across C calls, beyond the Microsoft ABI's 32-byte home area.
#else
    4;
#endif
static void emit_fpu_save_fetch_flags()
{
#ifdef _WIN32
    emit_mem(0x8b,0,RAX,RSP,-1,0,4);emit_mem(0x89,0,RAX,RSP,-1,0,fpu_fetch_flag);
#endif
}
static void emit_fpu_native(uint32_t instruction, unsigned index, const struct parallaxjit_config* config,
    unsigned char** exits, unsigned* exit_count,bool prefetched=false,unsigned fetch_start=0)
{
    unsigned fmt = (instruction >> 21) & 31, fn = instruction & 63;
    int integer = (fn >= 8 && fn <= 15) || fn == 36 || fn == 37;
    int out_single = fn == 32 || (fmt == 16 && fn != 33);
    if(prefetched) {
        emit_movimm(0,R13);
        bool line=index==fetch_start || ((config->virtual_pc+index*4)>>5)!=((config->virtual_pc+(index-1)*4)>>5);
        emit_fetch_native(config,index,fetch_start,0,line,true,fpu_fetch_flag);
        emit_pending_clocks(config,0,1,fpu_fetch_flag);
        emit_reg_op(0x85,R13,R13,0);auto *free_fetch=emit_jump(4);
        emit_mem_imm(0xc7,0,0,RSP,-1,0,8,0);
        emit_region_commit_native(config,index,0,0);
        patch_jump(free_fetch,out);
    }
    emit_state_imm(offsetof(struct r4300_core, instruction_pc.addr), config->virtual_pc + index * 4, 0);
    emit_mov64(R12, ARG1_REG); emit_movimm(instruction, ARG2_REG);
    emit_mem(0x8d, 1, ARG3_REG, RSP, -1, 0, 32);
    emit_movimm64((intptr_t)config->commits,ARG4_REG);
    auto prepare=config->fpu_prepare;
    if(prefetched)prepare=r4300_ops_jit_fpu_prepare_prefetched;
    emit_helper_call((const void*)prepare);
    emit_reg_op(0x85, RAX, RAX, 0); exits[(*exit_count)++] = emit_jump(4);
    if (fn >= 48) {
        emit_mem_imm(0x81,0,7,RSP,-1,0,48,0); unsigned char* unordered = emit_jump(5);
        emit_mxcsr(3,16);
        emit_mem_imm(0xc7,0,0,RSP,-1,0,20,0x1f80); emit_mxcsr(2,20);
        emit_mem(0x8b,1,RAX,RSP,-1,0,32); emit_mem(0x8b,1,RCX,RSP,-1,0,40);
        emit_xmm_bits(RAX,0,1); emit_xmm_bits(RCX,1,1);
        emit_movimm(0,R8); emit_movimm(0,R9);
        if(fmt==17) JIT(ucomisd(Xbyak::Xmm(0),Xbyak::Xmm(1)));
        else JIT(ucomiss(Xbyak::Xmm(0),Xbyak::Xmm(1))); /* UCOMISS/SD */
        if (fn & 2) emit_set_condition(R8,4);
        if (fn & 4) emit_set_condition(R9,2);
        emit_reg_op(0x09,R9,R8,0); emit_movimm(0,R9); emit_mxcsr(2,16);
        unsigned char* done = emit_b();
        patch_jump(unordered,out); emit_movimm(fn & 1,R8); emit_movimm(0,R9); patch_jump(done,out);
    } else if (fn >= 5 && fn <= 7) {
        emit_mem(0x8b, 1, R8, RSP, -1, 0, 32); emit_movimm(0, R9);
        if (fn != 6) {
            if (fmt == 16) emit_mov(R8, R8);
            emit_movimm64(fmt == 16 ? UINT64_C(0x80000000) : UINT64_C(0x8000000000000000), RAX);
            if (fn == 5) { JIT(not_(Xbyak::Reg64(RAX))); }
            emit_reg_op(fn == 5 ? 0x21 : 0x31, RAX, R8, 1);
        }
    } else {
        emit_mxcsr(3, 16); /* STMXCSR */
        emit_mem(0x8b, 0, RAX, RSP, -1, 0, 48);
        emit_movimm64((intptr_t)fpu_maps.mxcsr,RDX);
        emit_mem(0x8b,0,RDX,RDX,RAX,2,0);
        emit_mem(0x89, 0, RDX, RSP, -1, 0, 20); emit_mxcsr(2, 20); /* LDMXCSR */
        emit_mem(0x8b, 1, RAX, RSP, -1, 0, 32);
        emit_mem(0x8b, 1, RCX, RSP, -1, 0, 40);
        emit_xmm_bits(RAX, 0, 1); emit_xmm_bits(RCX, 1, 1);
        if (integer) {
            if(fmt==16) JIT(cvtss2si(Xbyak::Reg64(R8),Xbyak::Xmm(0)));
            else JIT(cvtsd2si(Xbyak::Reg64(R8),Xbyak::Xmm(0)));
        } else if (fn == 32 || fn == 33) {
            if (fmt == 16 || fmt == 17) {
                if(fmt==16) JIT(cvtss2sd(Xbyak::Xmm(0),Xbyak::Xmm(0))); else JIT(cvtsd2ss(Xbyak::Xmm(0),Xbyak::Xmm(0)));
            } else {
                if(fn==32) JIT(cvtsi2ss(Xbyak::Xmm(0),host_reg(RAX,fmt==21)));
                else JIT(cvtsi2sd(Xbyak::Xmm(0),host_reg(RAX,fmt==21)));
            }
            emit_xmm_bits(R8, 0, 0); if (out_single) emit_mov(R8, R8);
        } else {
            switch(fn) {
#define FP(n,ss,sd) case n: if(fmt==16) JIT(ss(Xbyak::Xmm(0),Xbyak::Xmm(n==4?0:1))); else JIT(sd(Xbyak::Xmm(0),Xbyak::Xmm(n==4?0:1))); break
            FP(0,addss,addsd); FP(1,subss,subsd); FP(2,mulss,mulsd); FP(3,divss,divsd); FP(4,sqrtss,sqrtsd);
#undef FP
            default: abort();
            }
            emit_xmm_bits(R8, 0, 0); if (out_single) emit_mov(R8, R8);
        }
        emit_mxcsr(3, 20); emit_mxcsr(2, 16);
        emit_mem(0x8b, 0, RAX, RSP, -1, 0, 20);
        emit_imm_op(4,RAX,63,0);
        emit_movimm64((intptr_t)fpu_maps.sse,RDX);
        JIT(movzx(Xbyak::Reg32(R9),host_mem(8,RDX,RAX,0,0)));
        if (integer && ((fn >= 12 && fn <= 15) || fn == 36)) {
            emit_imm_op(7,R8,INT32_MAX,1); unsigned char* too_high = emit_jump(15);
            emit_imm_op(7,R8,(uint32_t)INT32_MIN,1); unsigned char* too_low = emit_jump(12);
            unsigned char* word_ok = emit_b();
            patch_jump(too_high,out); patch_jump(too_low,out); emit_movimm(32,R9);
            patch_jump(word_ok,out); emit_mov(R8,R8);
        }
    }
    unsigned char* fast_done = emit_fpu_fast_finish(instruction,index,config,exits,exit_count);
    emit_mov64(R8,ARG3_REG); emit_mov(R9,ARG4_REG);
    emit_mov64(R12,ARG1_REG); emit_movimm(instruction,ARG2_REG);
    emit_helper_call((const void*)config->fpu_finish);
    emit_reg_op(0x85,RAX,RAX,0); exits[(*exit_count)++] = emit_jump(4);
    patch_jump(fast_done,out);
}
