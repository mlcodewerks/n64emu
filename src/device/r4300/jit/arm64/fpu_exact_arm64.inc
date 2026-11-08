/* Exact W -> D conversion cannot depend on FPCR rounding or set FPSR. */
static void emit_fpu_word_to_double(void)
{
    emit_mem(0x8b,0,T0,SP,-1,0,32);
    JIT(scvtf(DReg(0),WReg(T0)));JIT(fmov(XReg(CACHE0),DReg(0)));
    emit_movimm(0,CACHE1);
}
