/* Execute the repaired instructions through each architecture's actual RSP JIT. */
#define main full_rsp_unit_main
#include "../../src/device/rcp/rsp/p_rsp/jit_tests.cpp"
#undef main

int main(int argc, char **argv)
{
    Fixture f;
    unsigned checks = 0;
    for (uint32_t value : {0u,1u,0x7fffu,0x8000u,0x7fffffffu,0x80000000u,0xffff8000u,0xffffffffu})
        for (unsigned imm : {0u,1u,0x7fffu,0x8000u,0xffffu}) for (unsigned constant=0;constant<2;++constant) {
            std::fill(std::begin(f.imem),std::end(f.imem),0);
            f.cpu.get_state().sr[1]=value;
            unsigned index=0;
            if(constant) {
                f.imem[index++]=i_op(15,1,0,value>>16);
                f.imem[index++]=i_op(13,1,1,value);
            }
            f.imem[index++]=i_op(11,2,1,imm);f.imem[index]=13;
            f.restart();require(f.cpu.run()==MODE_BREAK,"SLTIU terminates");
            require(f.cpu.get_state().sr[2]==unsigned(value<uint32_t(int32_t(int16_t(imm)))),"SLTIU signed immediate, unsigned comparison");
            ++checks;
        }
    for(unsigned address : {0u,4u,4092u}) for(unsigned constant=0;constant<2;++constant) {
        std::fill(std::begin(f.imem),std::end(f.imem),0);
        f.cpu.get_state().sr[1]=address;f.dmem[address/4]=0xdeadbeef;
        unsigned index=0;if(constant)f.imem[index++]=i_op(9,1,0,address);
        f.imem[index++]=i_op(0x27,2,1,0);f.imem[index]=13;
        f.restart();require(f.cpu.run()==MODE_BREAK && f.cpu.get_state().sr[2]==0xdeadbeef,"LWU decodes as a word load");++checks;
    }
    std::printf("RSP: %u SLTIU/LWU native-JIT regressions passed.\n",checks);std::fflush(stdout);
    if(argc==2 && !std::strcmp(argv[1],"--scalar-only"))return 0;
    for(unsigned rt : {0u,1u,7u,8u,15u,16u,24u,31u}) for(unsigned e=0;e<16;++e)
        for(unsigned base : {0u,1u,7u,8u,15u,4095u}) for(int offset=-1;offset<=1;++offset) {
            auto &state=f.cpu.get_state();state.sr[1]=base;
            for(unsigned v=0;v<32;++v)for(unsigned lane=0;lane<8;++lane)
                state.cp2.regs[v].e[lane]=uint16_t((v*8+lane)*257u+0x1234u);
            std::fill(std::begin(f.dmem),std::end(f.dmem),0xa5a5a5a5u);
            std::array<unsigned char,4096> expected;expected.fill(0xa5);
            unsigned address=(base+offset*16)&4095,window=address&~7u;
            for(unsigned i=0;i<16;++i) {
                unsigned v=(rt&~7u)+(((i>>1)-(window>>1)+(e>>1))&7u);
                unsigned byte=(i+window)&15u;
                unsigned char value=uint8_t(state.cp2.regs[v].e[byte>>1]>>(byte&1?0:8));
                expected[(window+((address+i)&15u))&4095u]=value;
            }
            std::fill(std::begin(f.imem),std::end(f.imem),0);
            f.imem[0]=(0x3au<<26)|(1u<<21)|(rt<<16)|(11u<<11)|(e<<7)|(unsigned(offset)&127u);f.imem[1]=13;
            f.restart();require(f.cpu.run()==MODE_BREAK,"STV terminates");
            for(unsigned i=0;i<4096;++i)
                require(reinterpret_cast<unsigned char*>(f.dmem)[i^3u]==expected[i],"STV transpose across register groups/elements/DMEM wrap");
            ++checks;
        }
    std::printf("RSP: %u SLTIU/LWU/STV native-JIT regressions passed.\n",checks);
}
