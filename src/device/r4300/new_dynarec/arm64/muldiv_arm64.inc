
static void emit_muldiv(uint32_t instruction,enum r4300_opcode op)
{
    int word=op==R4300_OP_MULT || op==R4300_OP_MULTU || op==R4300_OP_DIV || op==R4300_OP_DIVU;
    int sign=op==R4300_OP_MULT || op==R4300_OP_DMULT || op==R4300_OP_DIV || op==R4300_OP_DDIV;
    int multiply=op==R4300_OP_MULT || op==R4300_OP_MULTU || op==R4300_OP_DMULT || op==R4300_OP_DMULTU;
    emit_state_load(gpr_offset((instruction>>21)&31),T0);
    emit_state_load(gpr_offset((instruction>>16)&31),T1);
    if(multiply) {
        if(word && sign) { emit_shift_reg(T1,4,29,1); emit_shift_reg(T1,7,29,1); }
        else if(word) { emit_mov(T0,T0); emit_mov(T1,T1); }
        if(!word) { if(sign) JIT(smulh(XReg(T2),XReg(T0),XReg(T1))); else JIT(umulh(XReg(T2),XReg(T0),XReg(T1))); } /* SMULH/UMULH */
        JIT(mul(XReg(T0),XReg(T0),XReg(T1))); /* MUL */
        if(word) { emit_mov64(T0,T2); emit_shift_reg(T2,5,32,1); }
    } else {
        if(word) {
            if(sign) emit_sign_extend_word();
            else { emit_mov(T0,T0); emit_mov(T1,T1); }
        }
        emit_reg_op(0x85,T1,T1,1); unsigned char* zero=emit_jump(4);
        emit_mov64(T0,T2);
        if(sign) JIT(sdiv(XReg(T0),XReg(T0),XReg(T1))); else JIT(udiv(XReg(T0),XReg(T0),XReg(T1))); /* SDIV/UDIV */
        JIT(msub(XReg(T2),XReg(T0),XReg(T1),XReg(T2))); /* MSUB remainder */
        unsigned char* done=emit_b();
        patch_jump(zero,out); emit_mov64(T0,T2);
        if(sign) {
            emit_shift_reg(T0,7,63,1); JIT(mvn(XReg(T0),XReg(T0))); emit_imm_op(1,T0,1,1);
        } else emit_movimm64(-1,T0);
        patch_jump(done,out);
    }
    if(word) { emit_sign_extend_word(); JIT(sxtw(XReg(T2),WReg(T2))); }
    emit_state_store(offsetof(struct r4300_core,lo),T0);
    emit_state_store(offsetof(struct r4300_core,hi),T2);
}
