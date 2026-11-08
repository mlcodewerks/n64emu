/* Every signed word has an exact binary64 representation. Build its bits
 * with word-pair operations, preserving the caller's x87/SSE/VFP state. */
void Compiler::fpu_word_to_double(uint32_t op,unsigned index,const n64emujit_config* config,Label& exit)
{
    host.imm(0,config->virtual_pc+index*4);host.store(0,STATE,offsetof(r4300_core,instruction_pc.addr));
    host.imm(0,op);host.argument(1,0);
    host.mov(0,FP);host.immediate(SUB,0,128);host.argument(2,0);
    host.imm(0,uint32_t(uintptr_t(config->commits)));host.argument(3,0);
    host.mov(0,STATE);host.argument(0,0);host.call(reinterpret_cast<const void*>(config->fpu_prepare));++calls;
    host.immediate(CMP,0,0);host.branch(exit,EQ);
    Label zero,positive,encoded;
    host.load(0,FP,-128);host.store(0,FP,-176);
    host.immediate(CMP,0,0);host.branch(zero,EQ);
    host.immediate(TEST,0,0x80000000u);host.branch(positive,EQ);
    host.imm(1,0);host.alu(SUB,1,0);host.mov(0,1);host.bind(positive);
    host.clz(1,0);host.imm(3,31);host.alu(SUB,3,1);
    host.imm(1,52);host.alu(SUB,1,3);
    host.immediate(ADD,3,1023);host.shift(3,LSL,20);host.store(3,FP,-180);
    host.imm(2,0);shift64(LSL);host.immediate(AND,2,0xfffffu);
    host.load(1,FP,-180);host.alu(OR,2,1);
    host.load(1,FP,-176);host.immediate(AND,1,0x80000000u);host.alu(OR,2,1);
    host.branch(encoded);host.bind(zero);host.imm(0,0);host.imm(2,0);host.bind(encoded);
    host.store(0,FP,-160);host.store(2,FP,-156);
    host.imm(0,0);host.argument(4,0);
    host.load(0,FP,-156);host.argument(3,0);
    host.load(0,FP,-160);host.argument(2,0);
    host.imm(0,op);host.argument(1,0);host.mov(0,STATE);host.argument(0,0);
    host.call(reinterpret_cast<const void*>(config->fpu_finish));++calls;
    host.immediate(CMP,0,0);host.branch(exit,EQ);
}
