    using namespace parallax32;
    unsigned lane=vs&7;
    unsigned selected=op!=0x33?e&7:e<2?lane:e<4?(lane&~1u)|(e&1):e<8?(lane&~3u)|(e&3):e&7;
    unsigned source=vec_offset(vt)+selected*2,dest=vec_offset(vd)+lane*2;
    unsigned div_in=offsetof(CPUState,cp2)+offsetof(CP2,div_in);
    unsigned div_out=offsetof(CPUState,cp2)+offsetof(CP2,div_out);
    unsigned dp=offsetof(CPUState,cp2)+offsetof(CP2,dp_flag);
    if(op==0x33){host.load(0,STATE,int32_t(source),2);host.store(0,STATE,int32_t(dest),2);return;}
    if(op==0x32||op==0x36) {
        host.load(0,STATE,int32_t(source),2);host.store(0,STATE,int32_t(div_in),2);
        host.load(0,STATE,int32_t(div_out),2);host.store(0,STATE,int32_t(dest),2);
        host.imm(0,1);host.store(0,STATE,int32_t(dp),1);return;
    }
    Label single,input_ready,no_adjust,zero_data,special,finish;
    if(op==0x31||op==0x35) {
        host.load(0,STATE,int32_t(dp),1);host.immediate(TEST,0,1);host.branch(single,EQ);
        host.load(0,STATE,int32_t(div_in),2);host.shift(0,LSL,16);
        host.load(1,STATE,int32_t(source),2);host.alu(OR,0,1);host.branch(input_ready);
    }
    host.bind(single);host.load(0,STATE,int32_t(source),2,true);host.bind(input_ready);
    host.imm(1,0);host.store(1,STATE,int32_t(dp),1);
    host.store(0,FP,-900);host.mov(2,0);host.shift(2,ASR,31);host.store(2,FP,-904);
    host.alu(XOR,0,2);host.store(0,FP,-908);
    host.load(1,FP,-900);host.immediate(CMP,1,uint32_t(-32768));host.branch(no_adjust,LE);
    host.alu(SUB,0,2);host.store(0,FP,-908);host.bind(no_adjust);
    host.immediate(CMP,0,0);host.branch(zero_data,EQ);
    host.load(1,FP,-900);host.immediate(CMP,1,uint32_t(-32768));host.branch(special,EQ);
    host.clz(1,0);host.store(1,FP,-912);host.shift_register(0,LSL,1);
    host.immediate(AND,0,0x7fc00000);host.shift(0,LSR,22);
    bool square_root=op==0x34||op==0x35;
    if(square_root) {
        host.immediate(AND,1,1);host.immediate(OR,0,0x200);host.immediate(AND,0,0x3fe);host.alu(OR,0,1);
    }
    host.shift(0,LSL,1);host.imm(2,uint32_t(uintptr_t(rsp_reciprocal_rom)));host.alu(ADD,0,2);
    host.load(0,0,0,2);host.immediate(OR,0,0x10000);host.shift(0,LSL,14);
    host.load(1,FP,-912);host.immediate(XOR,1,31);if(square_root)host.shift(1,LSR,1);
    host.shift_register(0,LSR,1);host.load(2,FP,-904);host.alu(XOR,0,2);host.branch(finish);
    host.bind(zero_data);host.imm(0,0x7fffffff);host.branch(finish);
    host.bind(special);host.imm(0,0xffff0000);host.bind(finish);
    host.store(0,STATE,int32_t(dest),2);host.shift(0,LSR,16);host.store(0,STATE,int32_t(div_out),2);
