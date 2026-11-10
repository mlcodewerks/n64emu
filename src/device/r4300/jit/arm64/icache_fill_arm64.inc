/* Same scheduler-clock/deadline policy as r4300_icache_fill_cycles(). */
static void emit_icache_fill_cycles(unsigned dest,unsigned unissued)
{
    JIT(sub(assembler->sp,assembler->sp,32));
    emit_mem(0x89,1,T0,SP,-1,0,0);emit_mem(0x89,1,T1,SP,-1,0,8);emit_mem(0x89,1,T2,SP,-1,0,16);
    emit_movimm(R4300_ICACHE_FILL_IDLE_CYCLES,T1);
    emit_mem(0x8b,1,T0,STATE,-1,0,offsetof(struct r4300_core,vi));
    emit_reg_op(0x85,T0,T0,1);auto *no_vi=emit_jump(4);
    emit_mem(0x8b,1,T0,T0,-1,0,offsetof(struct vi_controller,dp));
    emit_reg_op(0x85,T0,T0,1);auto *no_dp=emit_jump(4);
    emit_mem(0x8b,1,T2,T0,-1,0,offsetof(struct rdp_core,timing)+offsetof(struct rdp_timing,deadline));
    emit_mem(0x8b,0,T0,STATE,-1,0,offsetof(struct r4300_core,cp0.count_phase));
    emit_reg_op(0x01,CLOCKS,T0,1);
    if(unissued)emit_addimm64(T0,-int(unissued),T0);
    emit_shift_reg(T0,5,1,1);
    emit_mem(0x8b,1,T1,STATE,-1,0,offsetof(struct r4300_core,cp0.count_clock));
    emit_reg_op(0x01,T1,T0,1);
    emit_movimm(R4300_ICACHE_FILL_IDLE_CYCLES,T1);
    emit_reg_op(0x39,T0,T2,1);auto *idle=emit_jump(14);
    emit_movimm(R4300_ICACHE_FILL_BUSY_CYCLES,T1);
    patch_jump(idle,out);patch_jump(no_dp,out);patch_jump(no_vi,out);
    emit_mem(0x8b,1,T0,SP,-1,0,0);
    emit_reg_op(0x01,T1,dest,1);
    emit_mem(0x8b,1,T1,SP,-1,0,8);emit_mem(0x8b,1,T2,SP,-1,0,16);
    JIT(add(assembler->sp,assembler->sp,32));
}
