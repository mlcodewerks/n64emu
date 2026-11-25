#include "../fpu_flags.h"
#include "fpu_compare_arm64.inc"
#include "fpu_exact_arm64.inc"

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
        if (fs == 31 || fs == 0) emit_mem(0x63,1,T0,STATE,-1,0,fs == 31 ? offsetof(struct r4300_core,cp1.fcr31) : offsetof(struct r4300_core,cp1.fcr0));
        else emit_movimm(0,T0);
        if (rt) emit_state_store_raw(gpr_offset(rt),T0);
        return;
    }
    emit_state_load_raw((wide ? offsetof(struct r4300_core,cp1.regs_double) : offsetof(struct r4300_core,cp1.regs_simple)) + fs * sizeof(void*),T1);
    if (op == R4300_OP_MTC1 || op == R4300_OP_DMTC1) {
        emit_state_load_raw(gpr_offset(rt),T0); emit_mem(0x89,wide,T0,T1,-1,0,0);
    } else {
        emit_mem(wide ? 0x8b : 0x63,1,T0,T1,-1,0,0);
        if (rt) emit_state_store_raw(gpr_offset(rt),T0);
    }
}
static constexpr size_t fpu_issue_cycles=72; // AArch64 frame scratch above the branch checkpoint.
static void emit_fpu_charge_issue()
{
    emit_mem(0x8b,0,CLOCKS,SP,-1,0,fpu_issue_cycles);
    emit_mem(0x8b,0,T0,STATE,-1,0,offsetof(struct r4300_core,cp0.count_phase));
    emit_reg_op(0x01,CLOCKS,T0,0);emit_mov(T0,T2);emit_imm_op(4,T2,1,0);
    emit_mem(0x89,0,T2,STATE,-1,0,offsetof(struct r4300_core,cp0.count_phase));emit_shift_reg(T0,5,1,0);
    emit_mem(0x01,0,T0,STATE,-1,0,offsetof(struct r4300_core,cp0.regs[CP0_COUNT_REG]));
    emit_mem(0x01,1,T0,STATE,-1,0,offsetof(struct r4300_core,cp0.cycle_count));
    emit_mem(0x01,1,T0,STATE,-1,0,offsetof(struct r4300_core,cp0.count_clock));
}

/* The shared prepare has validated inputs and charged fetch/issue hazards.
 * Commit ordinary results here; exceptional outputs, enabled exceptions and
 * event boundaries retain the shared finish without repeating arithmetic. */
static unsigned char* emit_fpu_fast_finish(uint32_t instruction, unsigned index,
    const struct n64emujit_config* config, unsigned char** exits, unsigned* exit_count, bool deferred_issue)
{
    unsigned fn = instruction & 63, fmt = (instruction >> 21) & 31;
    int integer = (fn >= 8 && fn <= 15) || fn == 36 || fn == 37;
    int single = fn == 32 || (fmt == 16 && fn != 33);
    unsigned char* slow[5]; unsigned slow_count = 0;
    if (fn < 48 && fn != 6) {
        emit_mov(CACHE1,T0); emit_imm_op(4,T0,~UINT32_C(1),0);
        emit_reg_op(0x85,T0,T0,0); slow[slow_count++] = emit_jump(5);
        emit_mem_imm(0xf7,0,0,STATE,-1,0,offsetof(struct r4300_core,cp1.fcr31),0x80);
        slow[slow_count++] = emit_jump(5);
        if (!integer) {
            emit_mov64(CACHE0,T0);
            if (single) emit_imm_op(4,T0,0x7f800000,0);
            else { emit_shift_reg(T0,5,52,1); emit_imm_op(4,T0,2047,0); }
            emit_imm_op(7,T0,single ? 0x7f800000 : 2047,0); slow[slow_count++] = emit_jump(4);
            emit_reg_op(0x85,T0,T0,0); unsigned char* normal = emit_jump(5);
            emit_mov64(CACHE0,T0); emit_shift_reg(T0,4,single ? 41 : 12,1);
            emit_reg_op(0x85,T0,T0,1); slow[slow_count++] = emit_jump(5);
            patch_jump(normal,out);
        }
    }
    unsigned latency = fn >= 48 || fn == 6 ? 0 : r4300_base_cycles(instruction,CP0_STATUS_CU1) - 1;
    bool dynamic_latency=!r4300_fpu_compatibility_latency(config->cache_timing, config->compat_issue_clock) && fn<48 && fn!=6;
    if (dynamic_latency) emit_mem(0x8b,0,CLOCKS,SP,-1,0,52);
    else emit_movimm(latency,CLOCKS);
    if (deferred_issue) {
        emit_mem(0x8b,0,T1,SP,-1,0,fpu_issue_cycles);
        emit_reg_op(0x01,T1,CLOCKS,0);
    }
    emit_mem(0x8b,0,T0,STATE,-1,0,offsetof(struct r4300_core,cp0.count_phase));
    emit_reg_op(0x01,CLOCKS,T0,0);
    emit_shift_reg(T0,5,1,0);
    emit_mem(0x8b,1,T2,STATE,-1,0,offsetof(struct r4300_core,cp0.cycle_count));
    emit_reg_op(0x01,T0,T2,1); emit_reg_op(0x85,T2,T2,1); slow[slow_count++] = emit_jump(9);
    if (fn >= 48) {
        emit_mem_imm(0x81,0,4,STATE,-1,0,offsetof(struct r4300_core,cp1.fcr31),~UINT32_C(0x800000));
        emit_mov(CACHE0,T0); emit_shift_reg(T0,4,23,0);
        emit_mem(0x09,0,T0,STATE,-1,0,offsetof(struct r4300_core,cp1.fcr31));
    } else {
        /* The guards above admit only zero/inexact flags and disabled inexact
         * traps, matching the shared Off policy on the paired-word backends. */
        if (fn != 6 && !r4300_fp_status_relaxed(config->compat_issue_clock)) {
            emit_mov(CACHE1,T0); emit_shift_reg(T0,4,12,0);
            emit_mov(CACHE1,T2); emit_shift_reg(T2,4,2,0); emit_reg_op(0x09,T2,T0,0);
            emit_mem(0x09,0,T0,STATE,-1,0,offsetof(struct r4300_core,cp1.fcr31));
        }
        emit_mem(0x89,1,CACHE0,STATE,-1,0,offsetof(struct r4300_core,cp1.regs) + ((instruction >> 6) & 31) * sizeof(cp1_reg));
    }
    emit_mem_imm(0xc7,0,0,SP,-1,0,8,0);
    emit_region_commit_native(config,index,1,0);
    emit_movimm64((intptr_t)&breakloop,ADDRESS);
    emit_mem_imm(0x80,0,7,ADDRESS,-1,0,0,0); exits[(*exit_count)++] = emit_jump(5);
    unsigned char* done = emit_b();
    for (unsigned n = 0; n < slow_count; ++n) patch_jump(slow[n],out);
    return done;
}
static void emit_fpu_save_environment()
{
    JIT(mrs(XReg(T0),1,3,4,4,0)); emit_mem(0x89,1,T0,SP,-1,0,16); /* FPCR */
    JIT(mrs(XReg(T1),1,3,4,4,1)); emit_mem(0x89,1,T1,SP,-1,0,24); /* FPSR */
}
static void emit_fpu_environment_begin(bool saved)
{
    static const uint32_t rounding[4]={0,3u<<22,1u<<22,2u<<22};
    if (!saved) emit_fpu_save_environment();
    emit_mem(0x8b,0,T0,SP,-1,0,48);
    emit_movimm64((intptr_t)rounding,ADDRESS); emit_mem(0x8b,0,T0,ADDRESS,T0,2,0);
    JIT(msr(1,3,4,4,0,XReg(T0))); JIT(msr(1,3,4,4,1,assembler->xzr)); /* guest FPCR, clear FPSR */
}
static void emit_fpu_environment_end(void)
{
    JIT(mrs(XReg(T0),1,3,4,4,1)); /* MRS X9,FPSR */
    emit_mov64(T0,ADDRESS);
    emit_mem(0x8b,1,T0,SP,-1,0,16); JIT(msr(1,3,4,4,0,XReg(T0)));
    emit_mem(0x8b,1,T1,SP,-1,0,24); JIT(msr(1,3,4,4,1,XReg(T1)));
    /* FPSR IXC/UFC/OFC/DZC/IOC -> guest I/U/O/Z/V. */
    JIT(and_(WReg(ADDRESS),WReg(ADDRESS),31));
    emit_movimm64((intptr_t)fpu_maps.neon,T2);
    JIT(ldrb(WReg(CACHE1),ptr(XReg(T2),XReg(ADDRESS))));
}
enum { FPU_TEMP=T0, FPU_LEFT=CACHE0, FPU_RIGHT=CACHE1, FPU_INFO=ADDRESS,
       FPU_METADATA=T1, FPU_STATE=STATE, FPU_CLOCKS=CLOCKS, FPU_STACK=SP };
#include "../fpu_resident_prepare.inc"

/* Multiplication by a normal signed power of two is exact when its result
 * remains normal (or is signed zero). All special/boundary cases retain the
 * original controlled-FPCR arithmetic. Operand preparation is unchanged. */
static unsigned emit_exact_power_multiply(unsigned fmt,unsigned char **done)
{
    bool wide=fmt==17;
    unsigned char *slow[8];unsigned nslow=0;
    emit_mem(0x8b,wide,T0,SP,-1,0,32);emit_mem(0x8b,wide,T1,SP,-1,0,40);
    emit_mov64(T0,CACHE0);emit_mov64(T1,CACHE1);
    if(wide) {
        emit_shift_reg(CACHE0,4,12,1);emit_shift_reg(CACHE0,5,12,1);
        emit_shift_reg(CACHE1,4,12,1);emit_shift_reg(CACHE1,5,12,1);
    } else {
        emit_imm_op(4,CACHE0,0x7fffff,0);emit_imm_op(4,CACHE1,0x7fffff,0);
    }
    emit_mov64(T0,T2);emit_shift_reg(T2,5,wide?52:23,wide);emit_imm_op(4,T2,wide?2047:255,0);
    emit_mov64(T1,ADDRESS);emit_shift_reg(ADDRESS,5,wide?52:23,wide);emit_imm_op(4,ADDRESS,wide?2047:255,0);
    for(unsigned i=0;i<2;++i) {
        int exponent=i?ADDRESS:T2,fraction=i?CACHE1:CACHE0;
        emit_imm_op(7,exponent,wide?2047:255,0);slow[nslow++]=emit_jump(4);
        emit_reg_op(0x85,exponent,exponent,0);auto *normal=emit_jump(5);
        emit_reg_op(0x85,fraction,fraction,wide);slow[nslow++]=emit_jump(5);
        patch_jump(normal,out);
    }
    emit_mov64(T0,TARGET);emit_reg_op(0x31,T1,TARGET,wide);
    if(wide){emit_movimm64(UINT64_C(0x8000000000000000),CACHE2);emit_reg_op(0x21,CACHE2,TARGET,1);}
    else emit_imm_op(4,TARGET,UINT32_C(0x80000000),0);
    emit_reg_op(0x85,T2,T2,0);auto *zero_a=emit_jump(4);
    emit_reg_op(0x85,ADDRESS,ADDRESS,0);auto *zero_b=emit_jump(4);
    emit_reg_op(0x85,CACHE0,CACHE0,wide);auto *power_a=emit_jump(4);
    emit_reg_op(0x85,CACHE1,CACHE1,wide);slow[nslow++]=emit_jump(5);patch_jump(power_a,out);
    emit_reg_op(0x01,ADDRESS,T2,0);emit_addimm(T2,wide?-1024:-128,T2);
    emit_imm_op(7,T2,wide?2045:253,0);slow[nslow++]=emit_jump(7);
    emit_addimm(T2,1,T2);emit_shift_reg(T2,4,wide?52:23,wide);
    emit_reg_op(0x09,CACHE1,CACHE0,wide);emit_reg_op(0x09,T2,CACHE0,wide);emit_reg_op(0x09,TARGET,CACHE0,wide);
    emit_movimm(0,CACHE1);done[0]=emit_b();
    patch_jump(zero_a,out);patch_jump(zero_b,out);
    emit_mov64(TARGET,CACHE0);emit_movimm(0,CACHE1);done[1]=emit_b();
    for(unsigned i=0;i<nslow;++i)patch_jump(slow[i],out);
    return 2;
}

static void emit_fpu_prefetch(unsigned index, unsigned fetch_start, const n64emujit_config* config)
{
    emit_movimm(0,CLOCKS);
    bool line=index==fetch_start || ((config->virtual_pc+index*4)>>5)!=((config->virtual_pc+(index-1)*4)>>5);
    emit_fetch_native(config,index,fetch_start,0,line,true);
    emit_pending_clocks(config,0,1);
    emit_reg_op(0x85,CLOCKS,CLOCKS,0);auto *free_fetch=emit_jump(4);
    emit_mem_imm(0xc7,0,0,SP,-1,0,8,0);
    emit_region_commit_native(config,index,0,0);patch_jump(free_fetch,out);
}

static void emit_prefetched_transfer(uint32_t instruction,unsigned index,unsigned start,
    const struct n64emujit_config *config)
{
    enum r4300_opcode op=r4300_get_idec(instruction)->opcode;
    unsigned fs=(instruction>>11)&31,rt=(instruction>>16)&31;
    emit_state_imm(offsetof(struct r4300_core,instruction_pc.addr),config->virtual_pc+index*4,0);
    emit_fpu_prefetch(index,start,config);
    emit_movimm(1,CLOCKS);
    emit_mem(0x8b,1,T0,STATE,-1,0,offsetof(struct r4300_core,pipeline_load_mask));
    emit_mov64(T0,T2);emit_shift_reg(T2,5,32,1);
    emit_reg_op(0x85,T2,T2,0);auto *fpr_history=emit_jump(5);
    emit_imm_op(4,T0,rt?(UINT32_C(1)<<rt):0,0);auto *selected=emit_b();
    patch_jump(fpr_history,out);
    emit_movimm64(UINT64_C(1)<<(32+(fs&~1u)),T2);
    if(fs&1) {
        emit_mem_imm(0xf7,0,0,STATE,-1,0,offsetof(struct r4300_core,cp0.regs[CP0_STATUS_REG]),CP0_STATUS_FR);
        auto *paired=emit_jump(4);emit_movimm64(UINT64_C(1)<<(32+fs),T2);patch_jump(paired,out);
    }
    emit_reg_op(0x21,T2,T0,1);patch_jump(selected,out);
    emit_reg_op(0x85,T0,T0,1);auto *no_ldi=emit_jump(4);
    emit_addimm(CLOCKS,1,CLOCKS);patch_jump(no_ldi,out);
    emit_pipeline_clear();
    if(op==R4300_OP_MTC1 || op==R4300_OP_DMTC1) {
        emit_movimm64(UINT64_C(1)<<(32+(fs&~1u)),T0);
        if(fs&1) {
            emit_mem_imm(0xf7,0,0,STATE,-1,0,offsetof(struct r4300_core,cp0.regs[CP0_STATUS_REG]),CP0_STATUS_FR);
            auto *paired=emit_jump(4);emit_movimm64(UINT64_C(1)<<(32+fs),T0);patch_jump(paired,out);
        }
        emit_mem(0x89,1,T0,STATE,-1,0,offsetof(struct r4300_core,pipeline_load_mask));
    }
    emit_fpu_transfer(instruction,op);
    emit_mem_imm(0xc7,0,0,SP,-1,0,8,0);
    emit_region_commit_native(config,index,1,0);
}

static void emit_fpu_native(uint32_t instruction,unsigned index,const struct n64emujit_config* config,
    unsigned char** exits,unsigned* exit_count,bool prefetched=false,unsigned fetch_start=0,bool environment_saved=false)
{
    unsigned fmt=(instruction>>21)&31,fn=instruction&63;
    int integer=(fn>=8 && fn<=15)||fn==36||fn==37;
    int single=fn==32 || (fmt==16 && fn!=33);
    if (prefetched) emit_fpu_prefetch(index,fetch_start,config);
    emit_state_imm(offsetof(struct r4300_core,instruction_pc.addr),config->virtual_pc+index*4,0);
    auto* prepared = emit_fpu_resident_prepare(instruction,index,config,prefetched);
    if (prepared) emit_mem_imm(0xc7,0,0,SP,-1,0,fpu_issue_cycles,0);
    emit_mov64(STATE,ARG1_REG); emit_movimm(instruction,ARG2_REG);
    emit_mem(0x8d,1,ARG3_REG,SP,-1,0,32); emit_movimm64((intptr_t)config->commits,ARG4_REG);
    auto prepare=config->fpu_prepare;
    if(prefetched)prepare=r4300_ops_jit_fpu_prepare_prefetched;
    emit_helper_call((const void*)prepare);
    emit_reg_op(0x85,T0,T0,0); exits[(*exit_count)++]=emit_jump(4);
    if (prepared) patch_jump(prepared,out);
    unsigned char* exact_done[2];
    const unsigned exact_count = fn == 2 && (fmt == 16 || fmt == 17) ?
        emit_exact_power_multiply(fmt,exact_done) : 0;
    if(fn>=48) {
        emit_fpu_compare(fmt,fn);
    } else if(fmt==20 && fn==33) {
        emit_fpu_word_to_double();
    } else if(fn>=5 && fn<=7) {
        emit_mem(0x8b,1,CACHE0,SP,-1,0,32); emit_movimm(0,CACHE1);
        if(fn!=6) {
            if(fmt==16) emit_mov(CACHE0,CACHE0);
            emit_movimm64(fmt==16?UINT64_C(0x80000000):UINT64_C(0x8000000000000000),T0);
            if(fn==5) JIT(mvn(XReg(T0),XReg(T0)));
            emit_reg_op(fn==5?0x21:0x31,T0,CACHE0,1);
        }
    } else {
        emit_fpu_environment_begin(environment_saved);
        emit_mem(0x8b,1,T0,SP,-1,0,32); emit_mem(0x8b,1,T1,SP,-1,0,40);
        if(fmt==16) JIT(fmov(SReg(0),WReg(T0))); else JIT(fmov(DReg(0),XReg(T0))); if(fmt==16) JIT(fmov(SReg(1),WReg(T1))); else JIT(fmov(DReg(1),XReg(T1)));
        if(integer) {
            if(fmt==16) JIT(frintx(SReg(0),SReg(0))); else JIT(frintx(DReg(0),DReg(0))); /* FRINTX */
            if(fmt==16) JIT(fcvtzs(XReg(CACHE0),SReg(0))); else JIT(fcvtzs(XReg(CACHE0),DReg(0))); /* FCVTZS X23 */
        } else if(fn==32 || fn==33) {
            if(fmt==16 || fmt==17) { if(fmt==16) JIT(fcvt(DReg(0),SReg(0))); else JIT(fcvt(SReg(0),DReg(0))); }
            else { if(fn==32) { if(fmt==21) JIT(scvtf(SReg(0),XReg(T0))); else JIT(scvtf(SReg(0),WReg(T0))); } else { if(fmt==21) JIT(scvtf(DReg(0),XReg(T0))); else JIT(scvtf(DReg(0),WReg(T0))); } }
            if(single) JIT(fmov(WReg(CACHE0),SReg(0))); else JIT(fmov(XReg(CACHE0),DReg(0)));
        } else {
            switch(fn) {
#define FP(n,op) case n: if(fmt==16) JIT(op(SReg(0),SReg(0),SReg(1))); else JIT(op(DReg(0),DReg(0),DReg(1))); break
            FP(0,fadd); FP(1,fsub); FP(2,fmul); FP(3,fdiv);
#undef FP
            case 4: if(fmt==16) JIT(fsqrt(SReg(0),SReg(0))); else JIT(fsqrt(DReg(0),DReg(0))); break;
            default: abort();
            }
            if(single) JIT(fmov(WReg(CACHE0),SReg(0))); else JIT(fmov(XReg(CACHE0),DReg(0)));
        }
        emit_fpu_environment_end();
        if(integer && ((fn>=12 && fn<=15)||fn==36)) {
            emit_imm_op(7,CACHE0,INT32_MAX,1); unsigned char* high=emit_jump(15);
            emit_imm_op(7,CACHE0,(uint32_t)INT32_MIN,1); unsigned char* low=emit_jump(12);
            unsigned char* done=emit_b(); patch_jump(high,out); patch_jump(low,out);
            emit_movimm(32,CACHE1); patch_jump(done,out); emit_mov(CACHE0,CACHE0);
        }
    }
    for (unsigned i=0; i<exact_count; ++i) patch_jump(exact_done[i],out);
    unsigned char* done=emit_fpu_fast_finish(instruction,index,config,exits,exit_count,prepared != nullptr);
    if (prepared) emit_fpu_charge_issue();
    emit_mov64(CACHE0,ARG3_REG); emit_mov(CACHE1,ARG4_REG);
    emit_mov64(STATE,ARG1_REG); emit_movimm(instruction,ARG2_REG);
    emit_helper_call((const void*)config->fpu_finish);
    emit_reg_op(0x85,T0,T0,0); exits[(*exit_count)++]=emit_jump(4);
    patch_jump(done,out);
}
