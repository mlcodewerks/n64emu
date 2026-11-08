/* Prepared COP1 comparisons use ordered integer keys. This leaves FPCR and
 * FPSR untouched, including ambient flush-to-zero and exception enables.
 * The precise preparer owns NaNs, Invalid and the guest cause bits. */
static void emit_fpu_compare(unsigned fmt,unsigned fn)
{
    bool wide=fmt==17;
    emit_movimm(0,CACHE0);emit_movimm(0,CACHE1);
    emit_mem_imm(0x81,0,7,SP,-1,0,48,0);auto *unordered=emit_jump(5);
    if(fn&6) {
        emit_mem(0x8b,wide,T0,SP,-1,0,32);
        emit_mem(0x8b,wide,T1,SP,-1,0,40);
        emit_mov64(T0,T2);emit_reg_op(0x09,T1,T2,wide);
        emit_shift_reg(T2,4,1,wide);
        emit_reg_op(0x85,T2,T2,wide);auto *both_zero=emit_jump(4);
        if(fn&2) {
            emit_reg_op(0x39,T1,T0,wide);emit_set_condition(CACHE0,4);
        }
        if(fn&4) {
            emit_movimm64(wide?UINT64_C(0x8000000000000000):UINT64_C(0x80000000),ADDRESS);
            for(unsigned n=0;n<2;++n) {
                int reg=n?T1:T0;
                emit_mov64(reg,T2);emit_shift_reg(T2,7,wide?63:31,wide);
                emit_reg_op(0x09,ADDRESS,T2,wide);
                emit_reg_op(0x31,T2,reg,wide);
            }
            emit_reg_op(0x39,T1,T0,wide);emit_set_condition(CACHE1,2);
            emit_reg_op(0x09,CACHE1,CACHE0,0);
        }
        auto *ordered=emit_b();patch_jump(both_zero,out);
        emit_movimm((fn&2)?1:0,CACHE0);patch_jump(ordered,out);
    }
    auto *done=emit_b();patch_jump(unordered,out);emit_movimm(fn&1,CACHE0);
    patch_jump(done,out);emit_movimm(0,CACHE1);
}
