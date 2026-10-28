#ifndef PARALLAX_RSP_QUANTIZE_HPP
#define PARALLAX_RSP_QUANTIZE_HPP
#include "../state.hpp"

namespace RSP {
static inline bool quantize_opcode(unsigned op)
{ return op == 2 || op == 3 || op == 10 || op == 11; }

/* MPEG quantization and conditional accumulator rounding. Sources are
 * snapshotted before VD writes, including broadcast/aliased operands. */
static inline void execute_quantize(CPUState *state, uint32_t instruction)
{
    unsigned op=instruction&63, vd=(instruction>>6)&31, vs=(instruction>>11)&31;
    unsigned vt=(instruction>>16)&31, e=(instruction>>21)&15;
    int16_t source[8], operand[8];
    for(unsigned lane=0;lane<8;lane++) {
        unsigned selected=e<2?lane:e<4?(lane&~1u)|(e&1):e<8?(lane&~3u)|(e&3):e&7;
        source[lane]=(int16_t)state->cp2.regs[vs].e[lane];
        operand[lane]=(int16_t)state->cp2.regs[vt].e[selected];
    }
    for(unsigned lane=0;lane<8;lane++) {
        auto *acc=state->cp2.acc.e;
        int64_t value=int64_t(int16_t(acc[lane]))*INT64_C(0x100000000)+
            int64_t(acc[8+lane])*65536+acc[16+lane];
        int64_t result;
        if(op==3 || op==11) {
            int32_t product;
            if(op==3) {
                product=int32_t(source[lane])*operand[lane];
                if(product<0)product+=31;
                value=int64_t(product)*65536;
            } else {
                product=int32_t(value>>16);
                if(!(product&32)) {
                    if(product<0)product+=32;
                    else if(product>=32)product-=32;
                }
                value=int64_t(product)*65536+acc[16+lane];
            }
            result=int64_t(product)>>1;
        } else {
            if((op==2 && value>=0) || (op==10 && value<0))
                value+=int64_t(operand[lane])*((vs&1)?65536:1);
            value=int64_t(uint64_t(value)&UINT64_C(0xffffffffffff));
            if(value&INT64_C(0x800000000000))value-=INT64_C(0x1000000000000);
            result=value>>16;
        }
        acc[lane]=uint16_t(uint64_t(value)>>32);
        acc[8+lane]=uint16_t(uint64_t(value)>>16);
        acc[16+lane]=uint16_t(value);
        if(result>32767)result=32767;
        if(result<-32768)result=-32768;
        state->cp2.regs[vd].e[lane]=uint16_t(result)&((op==3 || op==11)?0xfff0:0xffff);
    }
}
template<unsigned Op> static inline void quantize_reference(CPUState *s,unsigned d,unsigned a,unsigned b,unsigned e)
{ execute_quantize(s,Op|(d<<6)|(a<<11)|(b<<16)|(e<<21)); }
}
#endif
