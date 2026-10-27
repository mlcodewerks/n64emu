#include "rsp_jit.hpp"
#include <algorithm>
#include <array>
#include <cstdio>
#include <cstdlib>
#include <random>

using namespace RSP;
static void require(bool condition, const char *message)
{
    if (!condition) { std::fprintf(stderr, "RSP JIT: %s\n", message); std::exit(1); }
}
struct Fixture
{
    JIT::CPU cpu;
    uint32_t imem[IMEM_WORDS] = {}, dmem[DMEM_WORDS] = {}, cr[16] = {}, irq = 0;
    Fixture()
    {
        cpu.set_imem(imem); cpu.set_dmem(dmem);
        auto &s = cpu.get_state();
        for (unsigned i = 0; i < 16; i++) s.cp0.cr[i] = cr + i;
        s.cp0.irq = &irq;
    }
    void restart()
    {
        auto &s = cpu.get_state(); s.pc = s.cycles = s.has_delay_slot = s.branch_target = 0;
        cpu.invalidate_imem();
    }
};
static uint32_t r_op(unsigned fn, unsigned d, unsigned a, unsigned b, unsigned shift = 0)
{ return (a << 21) | (b << 16) | (d << 11) | (shift << 6) | fn; }
static uint32_t i_op(unsigned op, unsigned d, unsigned a, uint16_t value)
{ return (op << 26) | (a << 21) | (d << 16) | value; }

static JIT::HostWord arguments8(JIT::HostWord a, JIT::HostWord b, JIT::HostWord c, JIT::HostWord d,
                              JIT::HostWord e, JIT::HostWord f, JIT::HostWord g, JIT::HostWord h)
{ return a + 3*b + 5*c + 7*d + 11*e + 13*f + 17*g + 19*h; }
static void call_abi()
{
    using namespace JIT;
    // This creates register-move cycles in both the Windows and System V ABIs,
    // as well as exercising all stack argument slots used by the emitter.
    for (unsigned pattern = 0; pattern < 2; pattern++) {
        Emitter emit; Allocator memory;
        emit.frame(256);
        for (unsigned i = 0; i < JIT_R_NUM; i++) emit.movi(JIT_R(i), 10+i);
        emit.movi(JIT_V(3), 20); emit.movi(JIT_V(4), 21);
        unsigned source[] = {JIT_R(3), JIT_R(2), JIT_R(1), JIT_R(0), JIT_V(3), JIT_R(4), JIT_V(4), JIT_R(2)};
        if (pattern) { source[4] = JIT_R(3); source[5] = JIT_R(2); }
        HostWord values[8];
        emit.prepare();
        for (unsigned i = 0; i < 8; i++) {
            values[i] = source[i] >= JIT_V0 ? 20 + source[i] - JIT_V(3) : 10 + source[i];
            emit.pushargr(source[i]);
        }
        emit.finishi(reinterpret_cast<void *>(arguments8)); emit.retval(JIT_R0); emit.retr(JIT_R0);
        HostWord size; emit.get_code(&size); void *code = memory.allocate_code(size);
        require(code != nullptr, "ABI test allocation"); emit.set_code(code,size); emit.emit();
        require(Allocator::commit_code(code,size), "ABI test executable memory");
        auto function = reinterpret_cast<int (*)()>(code);
        require(function() == arguments8(values[0],values[1],values[2],values[3],values[4],values[5],values[6],values[7]), "parallel ABI argument moves");
    }
}

static void scalar_registers()
{
    Fixture f;
    std::mt19937 random(0x525350);
    // Long basic blocks exercise register eviction, aliasing and sign/zero
    // extension. Each result is compared to 32-bit architectural arithmetic.
    for (unsigned trial = 0; trial < 500; trial++) {
        std::array<uint32_t, 32> expected = {};
        for (unsigned i = 1; i < 32; i++) expected[i] = random();
        std::copy(expected.begin(), expected.end(), f.cpu.get_state().sr);
        for (unsigned pc = 0; pc < 127; pc++) {
            unsigned a = random() % 32, b = random() % 32, d = random() % 32, shift = random() % 32;
            uint32_t x = expected[a], y = expected[b], value = 0, instr = 0;
            int16_t imm = int16_t(random());
            switch (random() % 23) {
            case 0: instr = r_op(0,d,a,b,shift); value = y << shift; break;
            case 1: instr = r_op(2,d,a,b,shift); value = y >> shift; break;
            case 2: instr = r_op(3,d,a,b,shift); value = int32_t(y) >> shift; break;
            case 3: instr = r_op(4,d,a,b); value = y << (x & 31); break;
            case 4: instr = r_op(6,d,a,b); value = y >> (x & 31); break;
            case 5: instr = r_op(7,d,a,b); value = int32_t(y) >> (x & 31); break;
            case 6: instr = r_op(0x21,d,a,b); value = x + y; break;
            case 7: instr = r_op(0x23,d,a,b); value = x - y; break;
            case 8: instr = r_op(0x24,d,a,b); value = x & y; break;
            case 9: instr = r_op(0x25,d,a,b); value = x | y; break;
            case 10: instr = r_op(0x26,d,a,b); value = x ^ y; break;
            case 11: instr = r_op(0x27,d,a,b); value = ~(x | y); break;
            case 12: instr = r_op(0x2a,d,a,b); value = int32_t(x) < int32_t(y); break;
            case 13: instr = r_op(0x2b,d,a,b); value = x < y; break;
            case 14: instr = i_op(9,d,a,imm); value = x + int32_t(imm); break;
            case 15: instr = i_op(10,d,a,imm); value = int32_t(x) < imm; break;
            case 16: instr = i_op(12,d,a,imm); value = x & uint16_t(imm); break;
            case 17: instr = i_op(14,d,a,imm); value = x ^ uint16_t(imm); break;
            case 18: instr = i_op(15,d,0,imm); value = uint32_t(uint16_t(imm)) << 16; break;
            case 19: instr = i_op(13,d,a,imm); value = x | uint16_t(imm); break;
            case 20: instr = i_op(11,d,a,imm); value = x < uint16_t(imm); break;
            case 21: instr = i_op(9,d,0,imm); value = uint32_t(int32_t(imm)); break;
            case 22: instr = r_op(0x26,d,a,a); value = 0; break;
            }
            f.imem[pc] = instr;
            if (d) expected[d] = value;
        }
        f.imem[127] = 13; f.restart();
        require(f.cpu.run() == MODE_BREAK, "random scalar program did not break");
        for (unsigned i = 0; i < 32; i++) if (f.cpu.get_state().sr[i] != expected[i]) {
            std::fprintf(stderr, "Scalar trial=%u r%u actual=%08x expected=%08x\n", trial, i, f.cpu.get_state().sr[i], expected[i]);
            for (unsigned pc = 0; pc < 128; pc++) std::fprintf(stderr, "%03x: %08x\n", pc * 4, f.imem[pc]);
            require(false, "scalar register mismatch");
        }
    }
}
static void branches_and_slices()
{
    Fixture f;
    f.imem[0] = i_op(9,1,0,100);
    f.imem[1] = i_op(9,2,0,0);
    f.imem[2] = i_op(9,1,1,uint16_t(-1));
    f.imem[3] = i_op(5,0,1,uint16_t(-2));
    f.imem[4] = i_op(9,2,2,3);
    f.imem[5] = 13;
    f.restart(); require(f.cpu.run() == MODE_BREAK, "loop did not break");
    auto cycles = f.cpu.get_state().cycles;
    require(f.cpu.get_state().sr[2] == 300, "loop delay slot");
    f.restart(); unsigned slices = 0;
    while (f.cpu.run(f.cpu.get_state().cycles + 8) == MODE_TIMESLICE) require(++slices < 1000, "slice loop failed to progress");
    require(slices > 0 && f.cpu.get_state().sr[2] == 300 && f.cpu.get_state().cycles == cycles, "timeslice resume");

    // A taken branch at the region boundary defers its delay slot to the next region.
    std::fill(std::begin(f.imem), std::end(f.imem), 0);
    f.imem[0] = (2u << 26) | 126;
    f.imem[126] = i_op(9,1,0,17);
    f.imem[127] = i_op(4,1,1,3);
    f.imem[128] = i_op(9,2,0,23);
    f.imem[129] = i_op(9,2,0,99);
    f.imem[131] = 13;
    f.restart(); require(f.cpu.run() == MODE_BREAK && f.cpu.get_state().sr[2] == 23, "region-boundary delay slot");
}
static void helpers_and_invalidation()
{
    Fixture f;
    auto &s = f.cpu.get_state();
    for (unsigned i = 0; i < 8; i++) { s.cp2.regs[9].e[i] = uint16_t(0x1234*i); s.cp2.regs[3].e[i] = uint16_t(0x9876+i); }
    for (unsigned i = 1; i < 32; i++) f.imem[i-1] = i_op(9,i,0,uint16_t(i*7));
    // Five helper arguments exercise the Win64 stack argument and ABI moves.
    f.imem[31] = (0x25u << 25) | (3 << 16) | (9 << 11) | (5 << 6) | 0x2c;
    f.imem[32] = r_op(0x21,4,1,31);
    f.imem[33] = i_op(9,6,0,4095);
    f.imem[34] = i_op(0x21,7,6,0);
    f.imem[35] = i_op(0x23,8,6,0);
    f.imem[36] = 13;
    auto *bytes = reinterpret_cast<uint8_t *>(f.dmem);
    for (unsigned i = 0; i < DMEM_SIZE; i++) bytes[i] = uint8_t(i*13+0x91);
    uint32_t half = (uint32_t(bytes[4095^3]) << 8) | bytes[0^3];
    uint32_t word = (half << 16) | (uint32_t(bytes[0x1^0x3]) << 8) | bytes[0x2^0x3];
    f.restart(); require(f.cpu.run() == MODE_BREAK, "helper program did not break");
    for (unsigned i = 0; i < 8; i++) require(s.cp2.regs[5].e[i] == uint16_t(s.cp2.regs[9].e[i] ^ s.cp2.regs[3].e[i]), "vector helper arguments");
    require(s.sr[4] == 224 && s.sr[7] == uint32_t(int32_t(int16_t(half))) && s.sr[8] == word, "registers across helpers or wrapping loads");

    f.imem[0] = i_op(9,1,0,42); f.imem[1] = 13; f.restart(); f.cpu.run();
    auto old = f.cpu.get_jit_block(0);
    f.imem[0] = i_op(9,1,0,43); f.restart(); f.cpu.run();
    require(s.sr[1] == 43 && f.cpu.get_jit_block(0) != old, "IMEM invalidation");
    f.imem[0] = i_op(9,1,0,42); f.restart(); f.cpu.run();
    require(s.sr[1] == 42 && f.cpu.get_jit_block(0) == old, "microcode cache reuse");
    f.cpu.set_cycle_timing(false); f.restart(); f.cpu.run(); require(s.cycles == 2, "untimed instruction count");
    f.cpu.set_cycle_timing(true); f.restart(); f.cpu.run(); require(s.cycles != 0, "timing-mode invalidation");
}
static void scalar_peepholes()
{
    using namespace JIT;
    Allocator memory;std::mt19937_64 random(0x50454550);
    const HostWord immediates[]={0,1,-1,4095,65535,INT32_MIN,INT32_MAX,-16};
    // Check full-width wrapping and signed comparisons, including destination
    // aliasing and the zero/nonzero branches used by dispatcher/timeslice code.
    for(unsigned op=0;op<7;op++)for(auto imm:immediates)for(unsigned alias=0;alias<3;alias++) {
        unsigned d=alias==0?JIT_R0:alias==1?JIT_R1:JIT_V(3);
        Emitter emit;emit.frame(256);emit.getarg(JIT_V0);
        emit.ldxi(JIT_R0,JIT_V0,0);emit.ldxi(JIT_R1,JIT_V0,8);
        switch(op) {
        case 0:emit.addi(d,JIT_R0,imm);break;
        case 1:emit.andi(d,JIT_R0,imm);break;
        case 2:emit.ori(d,JIT_R0,imm);break;
        case 3:emit.xori(d,JIT_R0,imm);break;
        case 4:emit.lti(d,JIT_R0,0);break;
        case 5:case 6:{
            auto *taken=op==5?emit.beqi(JIT_R0,0):emit.bnei(JIT_R0,0);
            emit.movi(d,0);auto *done=emit.jmpi();emit.patch(taken);emit.movi(d,1);emit.patch(done);break;
        }
        }
        emit.retr(d);HostWord size;emit.get_code(&size);
        void *code=memory.allocate_code(size);require(code!=nullptr,"peephole allocation");
        emit.set_code(code,size);emit.emit();require(Allocator::commit_code(code,size),"peephole executable memory");
        auto function=reinterpret_cast<uint64_t (*)(const uint64_t*)>(code);
        for(unsigned sample=0;sample<128;sample++) {
            uint64_t args[]={sample==0?0:sample==1?~uint64_t(0):sample==2?uint64_t(1)<<63:random(),random()};
            uint64_t value=args[0],expected=0,immediate=uint64_t(imm);
            switch(op) {
            case 0:expected=value+immediate;break;case 1:expected=value&immediate;break;
            case 2:expected=value|immediate;break;case 3:expected=value^immediate;break;
            case 4:expected=value>>63;break;case 5:expected=value==0;break;case 6:expected=value!=0;break;
            }
            require(function(args)==expected,"64-bit peephole semantics or register aliasing");
        }
    }
    std::puts("21,504 full-width scalar peephole cases passed.");
}
static void optimizer_boundaries()
{
    Fixture f;
    // The delay-slot value is observable on the taken path even though the
    // linear fallthrough overwrites it. The target has multiple predecessors.
    for (unsigned taken = 0; taken < 2; taken++) {
        std::fill(std::begin(f.imem), std::end(f.imem), 0);
        std::fill(std::begin(f.cpu.get_state().sr), std::end(f.cpu.get_state().sr), 0);
        f.cpu.get_state().sr[4] = taken;
        f.imem[1] = i_op(9,1,0,5);
        f.imem[2] = i_op(5,4,0,3); // target 6
        f.imem[3] = i_op(9,2,0,23);
        f.imem[4] = i_op(9,2,0,99);
        f.imem[5] = i_op(9,1,0,7);
        f.imem[6] = i_op(9,3,1,11);
        f.imem[7] = 13;
        f.restart(); require(f.cpu.run() == MODE_BREAK, "optimizer join program");
        require(f.cpu.get_state().sr[2] == (taken ? 23u : 99u), "DSE across a branch delay slot");
        require(f.cpu.get_state().sr[3] == (taken ? 16u : 18u), "constant fact escaped a control-flow join");
    }
    // A runtime load must kill a prior constant, including when rt == base.
    f.dmem[0] = 0x80000001u;
    f.imem[0] = 0; f.imem[1] = i_op(9,1,0,0);
    f.imem[2] = i_op(0x23,1,1,0);f.imem[3] = i_op(9,2,1,1);f.imem[4] = 13;
    f.restart();require(f.cpu.run()==MODE_BREAK && f.cpu.get_state().sr[2]==0x80000002u,"load invalidates a constant base/destination");
}
static void vector_quad_memory()
{
    Fixture f;std::mt19937 random(0x51564d45);unsigned cases=0;
    CPUState initial=f.cpu.get_state();
    for(unsigned i=1;i<32;i++)initial.sr[i]=random();initial.sr[0]=0;
    for(unsigned i=0;i<sizeof(initial.cp2);i++)reinterpret_cast<unsigned char*>(&initial.cp2)[i]=random();
    std::array<uint32_t,DMEM_WORDS> data,expected_data;
    for(auto &word:data)word=random();
    for(unsigned kind=0;kind<3;kind++)for(unsigned store=0;store<(kind?1u:2u);store++)for(int offset=-1;offset<=1;offset++)for(unsigned known=0;known<2;known++)for(unsigned address=0;address<4096;address++) {
        auto &s=f.cpu.get_state();s=initial;s.sr[1]=0xffff0000u|address;
        std::copy(data.begin(),data.end(),f.dmem);expected_data=data;
        CPUState expected=s;expected.dmem=expected_data.data();
        std::fill(std::begin(f.imem),std::end(f.imem),0);
        for(unsigned i=0;i<8;i++){f.imem[1+i]=i_op(9,2+i,2+i,i+3);expected.sr[2+i]+=i+3;}
        if(known){f.imem[9]=i_op(9,1,0,address);expected.sr[1]=address;}
        unsigned vt=address&31;
        f.imem[10]=((store?0x3a:0x32)<<26)|(1<<21)|(vt<<16)|((kind?kind+5:4)<<11)|(unsigned(offset)&127);
        if(kind==1)RSP_LPV(&expected,vt,0,offset,1);else if(kind==2)RSP_LUV(&expected,vt,0,offset,1);
        else if(store)RSP_SQV(&expected,vt,0,offset,1);else RSP_LQV(&expected,vt,0,offset,1);
        f.imem[11]=i_op(9,15,1,7);expected.sr[15]=expected.sr[1]+7;
        f.imem[12]=r_op(0x21,16,2,8);expected.sr[16]=expected.sr[2]+expected.sr[8];f.imem[13]=13;
        f.restart();require(f.cpu.run()==MODE_BREAK,"quad memory program terminates");
        require(!std::memcmp(s.sr,expected.sr,32*sizeof(uint32_t)),"quad transfer preserves scalar cache");
        require(!std::memcmp(&s.cp2,&expected.cp2,sizeof(s.cp2)),"quad vector/endian/partial semantics");
        require(std::equal(expected_data.begin(),expected_data.end(),f.dmem),"quad DMEM boundary/endian semantics");
        ++cases;
    }
    std::printf("%u aligned/unaligned, constant/dynamic vector quad/packed transfers match reference helpers.\n",cases);
}
static void vector_small_memory()
{
    Fixture f;std::mt19937 random(0x534d414c);unsigned cases=0;
    CPUState initial=f.cpu.get_state();for(unsigned i=1;i<32;i++)initial.sr[i]=random();initial.sr[0]=0;
    for(unsigned i=0;i<sizeof(initial.cp2);i++)reinterpret_cast<unsigned char*>(&initial.cp2)[i]=random();
    using Operation=void(*)(CPUState*,unsigned,unsigned,int,unsigned);
    const Operation loads[]={RSP_LBV,RSP_LSV,RSP_LLV,RSP_LDV,nullptr,nullptr,RSP_LPV,RSP_LUV},stores[]={RSP_SBV,RSP_SSV,RSP_SLV,RSP_SDV,nullptr,nullptr,RSP_SPV,RSP_SUV};
    std::array<uint32_t,DMEM_WORDS> data,expected_data;for(auto &word:data)word=random();
    for(unsigned kind=0;kind<8;kind++)for(unsigned store=0;store<2;store++)for(unsigned element=0;element<16;element++)for(unsigned location=0;location<32;location++)for(int offset=-1;offset<=1;offset++)for(unsigned known=0;known<2;known++) {
        if(kind==4 || kind==5)continue;
        unsigned address=location<16?location:4080+location-16,vt=(address+element)&31;
        auto &s=f.cpu.get_state();s=initial;s.sr[1]=0xffff0000u|address;
        std::copy(data.begin(),data.end(),f.dmem);expected_data=data;CPUState expected=s;expected.dmem=expected_data.data();
        std::fill(std::begin(f.imem),std::end(f.imem),0);
        for(unsigned i=0;i<8;i++){f.imem[1+i]=i_op(9,2+i,2+i,i+3);expected.sr[2+i]+=i+3;}
        if(known){f.imem[9]=i_op(9,1,0,address);expected.sr[1]=address;}
        f.imem[10]=((store?0x3a:0x32)<<26)|(1<<21)|(vt<<16)|(kind<<11)|(element<<7)|(unsigned(offset)&127);
        (store?stores[kind]:loads[kind])(&expected,vt,element,offset,1);
        f.imem[11]=i_op(9,15,1,7);expected.sr[15]=expected.sr[1]+7;
        f.imem[12]=r_op(0x21,16,2,8);expected.sr[16]=expected.sr[2]+expected.sr[8];f.imem[13]=13;
        f.restart();require(f.cpu.run()==MODE_BREAK,"small vector memory program terminates");
        require(!std::memcmp(s.sr,expected.sr,32*sizeof(uint32_t)),"small transfer preserves scalar cache");
        require(!std::memcmp(&s.cp2,&expected.cp2,sizeof(s.cp2)),"small transfer vector/endian/truncation semantics");
        require(std::equal(expected_data.begin(),expected_data.end(),f.dmem),"small transfer DMEM wrap/endian semantics");++cases;
    }
    std::printf("%u small vector transfers match reference helpers across elements and DMEM boundaries.\n",cases);
}
int main()
{
    call_abi(); scalar_registers(); branches_and_slices(); helpers_and_invalidation();scalar_peepholes();optimizer_boundaries();vector_quad_memory();vector_small_memory();
    std::puts("RSP scalar, register-cache, branch, helper, timeslice and invalidation checks passed.");
}
