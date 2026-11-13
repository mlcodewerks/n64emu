#include "headless.h"
#define main unused_legacy_timing_main
#include "../benchmarks/cpu_timing.c"
#undef main
static void fresh(unsigned accurate,unsigned phase)
{
    r4300_jit_destroy(&cpu);memset(&memory,0,sizeof(memory));memset(&mi,0,sizeof(mi));setup(0);
    cpu.cache_timing=accurate;cpu.compat_issue_clock=!accurate;cpu.cp0.regs[CP0_STATUS_REG]=CP0_STATUS_CU1|CP0_STATUS_FR;
    cpu.cp0.count_phase=phase;cpu.cp0.count_clock=1000;cpu.cp0.cycle_count=-100000;
    cpu.icache_fill_index=cpu.write_buffer_count=0;r4300_pipeline_reset(&cpu);
}
int main(void)
{
    unit_headless();unsigned cases=0;
    const struct {uint32_t op;unsigned clocks;} ops[]={
        {0,1},{0x00220018,5},{0x0022001a,37},{0x0022001c,8},{0x0022001e,69},
        {0x40816000,2},{0x44810000,1},{0x460208c0,3},{0x460208c2,5},
        {0x462208c2,8},{0x460208c3,29},{0x462208c3,58},{0x460008c4,29},{0x462008c4,58}
    };
    for(unsigned accurate=0;accurate<2;++accurate)for(unsigned phase=0;phase<2;++phase)
    for(unsigned i=0;i<sizeof(ops)/sizeof(ops[0]);++i){
        fresh(accurate,phase);cpu.regs[1]=9;cpu.regs[2]=3;
        if(ops[i].op==0x40816000)cpu.regs[1]=cpu.cp0.regs[CP0_STATUS_REG];
        if(((ops[i].op>>21)&31)==17){cpu.cp1.regs[1].float64=4.25;cpu.cp1.regs[2].float64=2.25;}
        else{cpu.cp1.regs[1].float32[0]=4.25;cpu.cp1.regs[2].float32[0]=2.25;}
        /* Supply a fetched opcode so the assertion isolates issue/execute
         * latency; the existing cache suite covers fetch and bus timing. */
        cpu.prefetch_pc=cpu.instruction_pc.addr;cpu.prefetch_op=ops[i].op;cpu.prefetch_valid=1;
        uint64_t before=r4300_cpu_clock(&cpu);r4300_ops_execute_one(&cpu);
        unsigned expected=ops[i].clocks+!accurate;
        if(r4300_cpu_clock(&cpu)-before!=expected)fprintf(stderr,"Timing op=%08x accurate=%u phase=%u got=%llu expected=%u\n",ops[i].op,accurate,phase,(unsigned long long)(r4300_cpu_clock(&cpu)-before),expected);
        assert(r4300_cpu_clock(&cpu)-before==expected);++cases;
    }
    const unsigned steps[]={1,2,3,16,69,1000,1048576};
    for(unsigned phase=0;phase<2;++phase)for(unsigned wrap=0;wrap<2;++wrap){
        fresh(0,phase);uint32_t start=wrap?UINT32_MAX-4:10;cpu.cp0.regs[CP0_COUNT_REG]=start;
        uint64_t total=phase;
        for(unsigned i=0;i<7;++i){cp0_step_cycles(&cpu.cp0,steps[i]);total+=steps[i];
            assert(cpu.cp0.regs[CP0_COUNT_REG]==(uint32_t)(start+total/2));
            assert(cpu.cp0.count_clock==1000+(int64_t)(total/2) && cpu.cp0.count_phase==(total&1));++cases;}
    }
    for(unsigned accurate=0;accurate<2;++accurate)for(unsigned write=0;write<2;++write){
        fresh(accurate,1);uint64_t before=r4300_cpu_clock(&cpu);
        r4300_data_access_cycles_explicit(&cpu,0xa4040010,0x04040010,write,0);
        assert(r4300_cpu_clock(&cpu)-before==(write?8:21));++cases;
    }
    for(unsigned phase=0;phase<2;++phase)for(unsigned cached=0;cached<2;++cached)
    for(unsigned write=0;write<2;++write){
        fresh(0,phase);uint64_t before=r4300_cpu_clock(&cpu);
        r4300_data_access_cycles_explicit(&cpu,cached?0x80000100:0xa0000100,0x100,write,cached);
        assert(r4300_cpu_clock(&cpu)-before==7);++cases;
    }
    for(unsigned accurate=0;accurate<2;++accurate)for(unsigned phase=0;phase<2;++phase){
        fresh(accurate,phase);cpu.cp1.regs[1].float32[0]=1;cpu.cp1.regs[2].float32[0]=2.25f;
        cpu.prefetch_pc=cpu.instruction_pc.addr;cpu.prefetch_op=0x460208c2;cpu.prefetch_valid=1;
        uint64_t before=r4300_cpu_clock(&cpu);r4300_ops_execute_one(&cpu);
        assert(r4300_cpu_clock(&cpu)-before==2+!accurate);
        assert(cpu.cp1.regs[3].float32[0]==2.25f && !cpu.cp1.fcr31);++cases;
    }
    fresh(1,0);assert(r4300_pipeline_cycles(&cpu,0x8c220000)==0);
    assert(r4300_pipeline_cycles(&cpu,0x00441821)==1);assert(r4300_pipeline_cycles(&cpu,0)==0);
    assert(r4300_pipeline_cycles(&cpu,0x00441821)==0);++cases;
    r4300_pipeline_reset(&cpu);assert(r4300_pipeline_cycles(&cpu,0x46041180)==0);
    assert(r4300_pipeline_cycles(&cpu,0x46083280)==1);++cases;
    r4300_pipeline_reset(&cpu);assert(r4300_pipeline_cycles(&cpu,0x46041032)==0);
    assert(r4300_pipeline_cycles(&cpu,0x45010000)==1);++cases;
    fresh(0,0);cpu.pipeline_load_mask=2;cpu.pipeline_fpu_result_mask=4;cpu.pipeline_fpu_cc=cpu.pipeline_cached_store=1;
    r4300_begin_prefetched_instruction_cycles(&cpu,0,0x80000000,1,0);
    assert(!cpu.pipeline_load_mask && !cpu.pipeline_fpu_result_mask && !cpu.pipeline_fpu_cc && !cpu.pipeline_cached_store);++cases;
    r4300_jit_destroy(&cpu);printf("Timing: %u On/Off latency, half-clock, wrap, RCP and pipeline contracts passed.\n",cases);return 0;
}
