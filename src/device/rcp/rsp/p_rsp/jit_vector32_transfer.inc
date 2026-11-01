    using namespace n64emu32;
    unsigned offset=vec_offset(vector),flag=std::min(vector&3u,2u);
    if(op==0) {
        if(!(element&1))host.load(0,STATE,int32_t(offset+element),2,true);
        else {
            host.load(0,STATE,int32_t(offset+(element^1)),1);host.shift(0,LSL,8);
            host.load(1,STATE,int32_t(offset+(((element+1)&15)^1)),1);host.alu(OR,0,1);
            host.shift(0,LSL,16);host.shift(0,ASR,16);
        }
        savev(scalar,0);
    } else if(op==4) {
        loadv(scalar,0);
        if(!(element&1))host.store(0,STATE,int32_t(offset+element),2);
        else {
            host.mov(1,0);host.shift(1,LSR,8);host.store(1,STATE,int32_t(offset+(element^1)),1);
            if(element!=15)host.store(0,STATE,int32_t(offset+((element+1)^1)),1);
        }
    } else if(op==2) {
        host.imm(0,0);host.store(0,FP,-920);
        for(unsigned part=0;part<2;part++)for(unsigned lane=0;lane<8;lane++) {
            host.load(0,STATE,int32_t(flag_offset(flag,1-part)+lane*2),2);
            host.shift(0,LSR,15);host.shift(0,LSL,part*8+lane);
            host.load(1,FP,-920);host.alu(OR,0,1);host.store(0,FP,-920);
        }
        host.shift(0,LSL,16);host.shift(0,ASR,16);savev(scalar,0);
    } else {
        for(unsigned part=0;part<2;part++)for(unsigned lane=0;lane<8;lane++) {
            if(part&&flag==2)host.imm(0,0);
            else {
                loadv(scalar,0);host.shift(0,LSR,part*8+lane);host.immediate(AND,0,1);
                // 0 - bit produces the architectural all-zero/all-one lane.
                host.imm(1,0);host.alu(SUB,1,0);host.mov(0,1);
            }
            host.store(0,STATE,int32_t(flag_offset(flag,1-part)+lane*2),2);
        }
    }
