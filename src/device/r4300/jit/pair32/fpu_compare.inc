/* The precise helpers retain issue, CU1, NaNs, flags, events and retirement.
 * Only the prepared comparison is lowered to integer word-pair branches.
 * No x87/SSE/VFP instruction or host FP-control update is needed. */
void Compiler::fpu_compare(uint32_t op,unsigned index,const n64emujit_config* config,Label& exit)
{
    unsigned fmt=(op>>21)&31,predicate=op&15;bool wide=fmt==17;
    host.imm(0,config->virtual_pc+index*4);host.store(0,STATE,offsetof(r4300_core,instruction_pc.addr));
    host.imm(0,op);host.argument(1,0);
    host.mov(0,FP);host.immediate(SUB,0,128);host.argument(2,0);
    host.imm(0,uint32_t(uintptr_t(config->commits)));host.argument(3,0);
    host.mov(0,STATE);host.argument(0,0);
    host.call(reinterpret_cast<const void*>(config->fpu_prepare));++calls;
    host.immediate(CMP,0,0);host.branch(exit,EQ);
    Label unordered,equal,yes,no,done,different,nonzero,negative,sign_different;
    host.load(0,FP,-112);host.immediate(CMP,0,0);host.branch(unordered,NE);
    if(predicate&6) {
        host.load(0,FP,-128);host.load(1,FP,-120);
        if(wide) {
            host.alu(OR,0,1);host.immediate(CMP,0,0);host.branch(nonzero,NE);
            host.load(2,FP,-124);host.load(3,FP,-116);host.alu(OR,2,3);
            host.shift(2,LSL,1);host.immediate(CMP,2,0);host.branch(equal,EQ);
            host.bind(nonzero);host.load(0,FP,-128);host.load(1,FP,-120);
            host.load(2,FP,-124);host.load(3,FP,-116);
            host.alu(CMP,2,3);host.branch(different,NE);
        } else {
            host.mov(2,0);host.alu(OR,2,1);host.shift(2,LSL,1);
            host.immediate(CMP,2,0);host.branch(equal,EQ);
        }
        host.alu(CMP,0,1);host.branch(equal,EQ);host.bind(different);
        if(predicate&4) {
            host.mov(2,wide?2:0);host.alu(XOR,2,wide?3:1);
            host.immediate(TEST,2,0x80000000u);host.branch(sign_different,NE);
            if(wide)host.load(2,FP,-124);else host.mov(2,0);
            host.immediate(TEST,2,0x80000000u);host.branch(negative,NE);
            if(wide){host.alu(CMP,2,3);host.branch(yes,LO);host.branch(no,HI);}
            host.alu(CMP,0,1);host.branch(yes,LO);host.branch(no);
            host.bind(negative);
            if(wide){host.alu(CMP,2,3);host.branch(yes,HI);host.branch(no,LO);}
            host.alu(CMP,0,1);host.branch(yes,HI);host.branch(no);
            host.bind(sign_different);host.load(0,FP,wide?-124:-128);
            host.immediate(TEST,0,0x80000000u);host.branch(yes,NE);
        }
        host.branch(no);
    } else host.branch(no);
    host.bind(equal);host.imm(0,(predicate&2)?1:0);host.branch(done);
    host.bind(yes);host.imm(0,1);host.branch(done);
    host.bind(no);host.imm(0,0);host.branch(done);
    host.bind(unordered);host.imm(0,predicate&1);host.bind(done);
    host.store(0,FP,-160);
    /* cdecl uses five stack words. AAPCS puts the uint64_t result in r2/r3
     * and its final flags argument at SP. Populate r0 last in either ABI. */
    host.imm(0,0);host.argument(4,0);host.argument(3,0);
    host.load(0,FP,-160);host.argument(2,0);
    host.imm(0,op);host.argument(1,0);host.mov(0,STATE);host.argument(0,0);
    host.call(reinterpret_cast<const void*>(config->fpu_finish));++calls;
    host.immediate(CMP,0,0);host.branch(exit,EQ);
}
