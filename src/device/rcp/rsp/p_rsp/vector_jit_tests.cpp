#include "rsp_jit.hpp"
#include <array>
#include <cstdio>
#include <cstdlib>
#include <random>
using namespace RSP;
using VUOp=void (*)(CPUState*,unsigned,unsigned,unsigned,unsigned);
static const VUOp operations[64]={
 RSP_VMULF,RSP_VMULU,nullptr,nullptr,RSP_VMUDL,RSP_VMUDM,RSP_VMUDN,RSP_VMUDH,RSP_VMACF,RSP_VMACU,nullptr,nullptr,RSP_VMADL,RSP_VMADM,RSP_VMADN,RSP_VMADH,
 RSP_VADD,RSP_VSUB,nullptr,RSP_VABS,RSP_VADDC,RSP_VSUBC,nullptr,nullptr,nullptr,nullptr,nullptr,nullptr,nullptr,RSP_VSAR,nullptr,nullptr,
 RSP_VLT,RSP_VEQ,RSP_VNE,RSP_VGE,RSP_VCL,RSP_VCH,RSP_VCR,RSP_VMRG,RSP_VAND,RSP_VNAND,RSP_VOR,RSP_VNOR,RSP_VXOR,RSP_VNXOR,nullptr,nullptr,
 RSP_VRCP,RSP_VRCPL,RSP_VRCPH,RSP_VMOV,RSP_VRSQ,RSP_VRSQL,RSP_VRSQH,RSP_VNOP,nullptr,nullptr,nullptr,nullptr,nullptr,nullptr,nullptr,nullptr};
static unsigned transfer_tests(JIT::Allocator &memory,std::mt19937 &random)
{
 unsigned cases=0;
 for(unsigned op:{0u,2u,4u,6u})for(unsigned rd=0;rd<32;rd++)for(unsigned e=0;e<16;e++)for(unsigned reg:{unsigned(JIT::JIT_R0),JIT::JIT_R(JIT::JIT_R_NUM-1),JIT::JIT_V(JIT::JIT_V_NUM-1)}) {
  JIT::Emitter emit;emit.frame(256);emit.getarg(JIT::JIT_V0);
  if(op>=4)emit.ldxi_i(reg,JIT::JIT_V0,offsetof(CPUState,sr)+4);
  emit.vector_transfer(op,reg,rd,e);
  if(op<4)emit.stxi_i(offsetof(CPUState,sr)+4,JIT::JIT_V0,reg);
  emit.movi(JIT::JIT_R0,0);emit.retr(JIT::JIT_R0);JIT::HostWord size;emit.get_code(&size);
  auto code=memory.allocate_code(size);if(!code)std::abort();emit.set_code(code,size);emit.emit();if(!JIT::Allocator::commit_code(code,size))std::abort();
  auto function=reinterpret_cast<void (*)(CPUState*)>(code);
  for(unsigned sample=0;sample<24;sample++) {
   CPUState actual={},expected={};auto bytes=reinterpret_cast<uint8_t*>(&actual.cp2);for(unsigned i=0;i<sizeof(CP2);i++)bytes[i]=uint8_t(random());actual.sr[1]=random();expected=actual;
   if(op==0)RSP_MFC2(&expected,1,rd,e);if(op==2)RSP_CFC2(&expected,1,rd);if(op==4)RSP_MTC2(&expected,1,rd,e);if(op==6)RSP_CTC2(&expected,1,rd);
   function(&actual);++cases;
   if(std::memcmp(&actual.cp2,&expected.cp2,sizeof(CP2))||actual.sr[1]!=expected.sr[1]){std::fprintf(stderr,"COP2 transfer mismatch op=%u rd=%u e=%u reg=%u sample=%u\n",op,rd,e,reg,sample);std::exit(1);}
  }
 }
 return cases;
}
static unsigned block_tests(std::mt19937 &random)
{
 JIT::CPU cpu;uint32_t imem[IMEM_WORDS]={},dmem[DMEM_WORDS]={},cr[16]={},irq=0;
 cpu.set_imem(imem);cpu.set_dmem(dmem);auto &s=cpu.get_state();s.cp0.irq=&irq;
 for(unsigned i=0;i<16;i++)s.cp0.cr[i]=cr+i;
 for(unsigned i=1;i<32;i++)imem[i-1]=(9u<<26)|(i<<16)|(i*7);
 imem[32]=(1<<21)|(31<<16)|(2<<11)|0x21;imem[33]=13;
 for(unsigned op=0;op<64;op++)for(unsigned e=0;e<16;e++) {
  auto *bytes=reinterpret_cast<uint8_t*>(&s.cp2);for(unsigned i=0;i<sizeof(CP2);i++)bytes[i]=uint8_t(random());
  CPUState expected={};expected.cp2=s.cp2;
  auto reference=operations[op]?operations[op]:RSP_RESERVED;reference(&expected,5,9,3,e);
  imem[31]=(0x25u<<25)|(e<<21)|(3<<16)|(9<<11)|(5<<6)|op;
  s.pc=s.cycles=s.has_delay_slot=s.branch_target=0;cpu.invalidate_imem();
  if(cpu.run()!=MODE_BREAK || std::memcmp(&s.cp2,&expected.cp2,sizeof(CP2))){std::fprintf(stderr,"Vector block mismatch op=%02x e=%u\n",op,e);std::exit(1);}
  for(unsigned i=0;i<32;i++)if(s.sr[i]!=(i==2?224:i*7)){std::fprintf(stderr,"Vector leaf clobbered scalar register %u op=%02x\n",i,op);std::exit(1);}
 }
 return 1024;
}
static unsigned sequence_tests(JIT::Allocator &memory,std::mt19937 &random)
{
 unsigned cases=0;size_t fused_bytes=0,separate_bytes=0;
 JIT::CPU cpu;uint32_t imem[IMEM_WORDS]={},dmem[DMEM_WORDS]={},cr[16]={},irq=0;
 cpu.set_imem(imem);cpu.set_dmem(dmem);auto &state=cpu.get_state();state.cp0.irq=&irq;
 for(unsigned i=0;i<16;i++)state.cp0.cr[i]=cr+i;
 const unsigned ops[]={0,1,4,5,6,7,8,9,12,13,14,15,16,17,19,20,21,29,32,33,34,35,36,37,38,39,40,41,42,43,44,45,55,63};
 for(unsigned trial=0;trial<2048;trial++) {
  unsigned count=2+random()%7;uint32_t instructions[8];
  JIT::Emitter separate;separate.frame(256);separate.getarg(JIT::JIT_V0);
  for(unsigned i=0;i<count;i++) {
   unsigned op=ops[random()%(sizeof(ops)/sizeof(ops[0]))];
   unsigned vd=random()%32,vs=random()%32,vt=random()%32,e=random()%16;
   // Dense aliases exercise forwarded registers, flags and accumulator chains.
   if(trial%3==0){vd%=4;vs%=4;vt%=4;}
   if(trial%8==0){op=i?16:44;vd=vs=vt=0;e=0;}
   if(trial%8==1){op=i?15:7;vd=vs=vt=i%4;e=8+i%8;}
   instructions[i]=(0x25u<<25)|(e<<21)|(vt<<16)|(vs<<11)|(vd<<6)|op;
   separate.vector_instruction(op,vd,vs,vt,e);
  }
  separate.movi(JIT::JIT_R0,0);separate.retr(JIT::JIT_R0);JIT::HostWord original;separate.get_code(&original);separate_bytes+=original;
  JIT::Emitter emit;emit.frame(256);emit.getarg(JIT::JIT_V0);emit.vector_sequence(instructions,count);
  emit.movi(JIT::JIT_R0,0);emit.retr(JIT::JIT_R0);JIT::HostWord size;emit.get_code(&size);fused_bytes+=size;
  auto code=memory.allocate_code(size);if(!code)std::abort();emit.set_code(code,size);emit.emit();if(!JIT::Allocator::commit_code(code,size))std::abort();
  auto function=reinterpret_cast<void (*)(CPUState*)>(code);
  // First VU stays separate in the block, all following VUs may be fused.
  imem[0]=0;std::copy(instructions,instructions+count,imem+1);imem[count+1]=13;
  for(unsigned sample=0;sample<16;sample++) {
   CPUState actual={},expected={};auto bytes=reinterpret_cast<uint8_t*>(&actual.cp2);for(unsigned i=0;i<sizeof(CP2);i++)bytes[i]=uint8_t(random());
   if(sample<8)for(auto &flag:actual.cp2.flags)for(auto &lane:flag.e)lane=(random()&1)?0xffff:0;
   expected=actual;
   for(unsigned i=0;i<count;i++) {
    uint32_t instr=instructions[i];unsigned op=instr&63;
    (operations[op]?operations[op]:RSP_RESERVED)(&expected,(instr>>6)&31,(instr>>11)&31,(instr>>16)&31,(instr>>21)&15);
   }
   function(&actual);++cases;
   if(std::memcmp(&actual.cp2,&expected.cp2,sizeof(CP2))){std::fprintf(stderr,"Fused sequence mismatch trial=%u sample=%u count=%u\n",trial,sample,count);std::exit(1);}
   if(sample==0) {
    state.cp2={};std::memcpy(&state.cp2,bytes,sizeof(CP2));
    // Recompute expected from the same already-executed state for integration.
    for(unsigned i=0;i<count;i++){uint32_t v=instructions[i];unsigned op=v&63;(operations[op]?operations[op]:RSP_RESERVED)(&expected,(v>>6)&31,(v>>11)&31,(v>>16)&31,(v>>21)&15);}
    state.pc=state.cycles=state.has_delay_slot=state.branch_target=0;cpu.invalidate_imem();
    if(cpu.run()!=MODE_BREAK || std::memcmp(&state.cp2,&expected.cp2,sizeof(CP2))){std::fprintf(stderr,"Fused block mismatch trial=%u\n",trial);std::exit(1);}
   }
  }
 }
 if(fused_bytes>=separate_bytes){std::fprintf(stderr,"Fusion did not reduce sequence code size\n");std::exit(1);}
 std::printf("Sequence code bytes: %zu fused / %zu separate; %u differential cases and 2048 integrated blocks passed.\n",fused_bytes,separate_bytes,cases);
 return cases;
}

int main()
{
 std::mt19937 random(0x564543);JIT::Allocator memory;unsigned cases=0;size_t optimized_bytes=0,original_bytes=0;
 static const uint16_t edges[]={0,1,0xffff,0x7fff,0x8000,0x8001,0x4000,0xc000};
 for(unsigned op=0;op<64;op++)for(unsigned e=0;e<16;e++)for(unsigned alias=0;alias<8;alias++) {
  unsigned vs=alias==7?0:(9+e+alias)&31,vt=alias==6?0:(3+e*2+alias)&31,vd=alias==0?5:alias==1?vs:alias==2?vt:alias==3?0:alias==4?31:alias==5?vs:5;
  if(alias==5)vt=vs;
  JIT::Emitter emit;emit.frame(256);emit.getarg(JIT::JIT_V0);emit.vector_instruction(op,vd,vs,vt,e);emit.movi(JIT::JIT_R0,0);emit.retr(JIT::JIT_R0);
  JIT::HostWord size;emit.get_code(&size);optimized_bytes+=size;
  JIT::Emitter original(false);original.frame(256);original.getarg(JIT::JIT_V0);original.vector_instruction(op,vd,vs,vt,e);original.movi(JIT::JIT_R0,0);original.retr(JIT::JIT_R0);
  JIT::HostWord before;original.get_code(&before);original_bytes+=before;
  void *code=memory.allocate_code(size);if(!code)std::abort();emit.set_code(code,size);emit.emit();if(!JIT::Allocator::commit_code(code,size))std::abort();
  auto function=reinterpret_cast<void (*)(CPUState*)>(code);
  for(unsigned sample=0;sample<96;sample++) {
   CPUState actual={},expected={};auto *bytes=reinterpret_cast<uint8_t*>(&actual.cp2);
   for(unsigned i=0;i<sizeof(CP2);i++)bytes[i]=uint8_t(random());
   if(sample<32)for(unsigned v=0;v<32;v++)for(unsigned lane=0;lane<8;lane++)actual.cp2.regs[v].e[lane]=edges[(sample+v+lane)%8];
   if(sample<64)for(auto &flag:actual.cp2.flags)for(auto &lane:flag.e)lane=(random()&1)?0xffff:0;
   if(sample<32){actual.cp2.div_in=int16_t(edges[sample%8]);actual.cp2.dp_flag=int8_t(sample&1);}
   expected=actual;auto reference=operations[op]?operations[op]:RSP_RESERVED;
   reference(&expected,vd,vs,vt,e);function(&actual);++cases;
   if(std::memcmp(&actual.cp2,&expected.cp2,sizeof(CP2))){
    std::fprintf(stderr,"Native vector mismatch op=%02x e=%u alias=%u sample=%u\n",op,e,alias,sample);
    auto *a=reinterpret_cast<const uint16_t*>(&actual.cp2),*b=reinterpret_cast<const uint16_t*>(&expected.cp2);
    for(unsigned i=0;i<sizeof(CP2)/2;i++)if(a[i]!=b[i])std::fprintf(stderr,"word[%u]: %04x expected %04x\n",i,a[i],b[i]);
    return 1;
   }
  }
 }
 unsigned transfers=transfer_tests(memory,random);
 unsigned blocks=block_tests(random);
 unsigned sequences=sequence_tests(memory,random);(void)sequences;
 if(optimized_bytes>=original_bytes){std::fprintf(stderr,"Vector optimization did not reduce aggregate code size\n");return 1;}
 std::printf("VU code bytes: %zu optimized / %zu original.\n",optimized_bytes,original_bytes);
 std::printf("Native vector differential tests passed: %u VU, %u COP2 transfer and %u scalar-cache/block cases.\n",cases,transfers,blocks);
}
