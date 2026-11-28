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

static void check_scaled_jit(unsigned accurate, unsigned shift)
{
    uint64_t result[7];
    for (unsigned native = 0; native <= 1; ++native) {
        setup(native);
        cpu.cache_timing = accurate;
        cpu.compat_issue_clock = !accurate;
        cpu.cp0.overclock_shift = shift;
        cpu.cp0.count_phase = (2u << shift) - 1;
        cpu.cp0.interrupt_handlers[4].callback = stop_event;
        code[0] = 0x24210001; /* ADDIU r1, r1, 1 */
        code[1] = 0x24230007; /* ADDIU r3, r1, 7 */
        code[2] = 0x00032080; /* SLL r4, r3, 2 */
        code[3] = 0x40054800; /* MFC0 r5, COUNT */
        code[4] = 0x1000fffb; /* BEQ zero, zero, back to code[0] */
        code[5] = 0x24420001; /* ADDIU r2, r2, 1 in the delay slot */
        add_interrupt_event(&cpu.cp0, PI_INT, 109);
        if (native) r4300_jit_run(&cpu);
        else while (!breakloop) r4300_ops_execute_one(&cpu);
        const uint64_t observed[] = {cpu.regs[1], cpu.regs[2], cpu.regs[3], cpu.regs[4], cpu.regs[5],
            cpu.instruction_pc.addr, r4300_cpu_clock(&cpu)};
        assert(cpu.regs[1] > 1 && cpu.cp0.count_clock >= 109);
        if (!native) memcpy(result, observed, sizeof(result));
        else {
            assert(!memcmp(result, observed, sizeof(result)));
            uint64_t stats[2];
            r4300_jit_optimization_stats(&cpu, stats);
            assert(stats[0] > 0);
        }
    }
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
    /* Cached Compatible must share issue and D-cache-hit clocks, and retain
     * operand-dependent FPU shortcuts, at either COUNT half-clock phase. */
    for(unsigned phase=0;phase<2;++phase) {
        fresh(1,phase);cpu.compat_issue_clock=1;
        cpu.prefetch_pc=cpu.instruction_pc.addr;cpu.prefetch_op=0;cpu.prefetch_valid=1;
        uint64_t before=r4300_cpu_clock(&cpu);r4300_ops_execute_one(&cpu);
        assert(r4300_cpu_clock(&cpu)-before==1);++cases;
        fresh(1,phase);cpu.compat_issue_clock=1;
        cpu.regs[2]=(int64_t)(int32_t)0x80000000;
        cpu.dcache_tags[0]=1;cpu.dcache_words[0][0]=123;
        before=r4300_cpu_clock(&cpu);
        const uint32_t hit_ops[]={0x8c430000,0x24840001};
        for(unsigned i=0;i<2;++i) {
            cpu.prefetch_pc=cpu.instruction_pc.addr;cpu.prefetch_op=hit_ops[i];cpu.prefetch_valid=1;
            r4300_ops_execute_one(&cpu);
        }
        assert(r4300_cpu_clock(&cpu)-before==2 && cpu.regs[3]==123);++cases;
        fresh(1,phase);cpu.compat_issue_clock=1;
        cpu.cp1.regs[1].float32[0]=1;cpu.cp1.regs[2].float32[0]=2.25f;
        cpu.prefetch_pc=cpu.instruction_pc.addr;cpu.prefetch_op=0x460208c2;cpu.prefetch_valid=1;
        before=r4300_cpu_clock(&cpu);r4300_ops_execute_one(&cpu);
        assert(r4300_cpu_clock(&cpu)-before==2 && cpu.cp1.regs[3].float32[0]==2.25f);++cases;
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
    for (unsigned shift = 1; shift <= 2; ++shift) {
        unsigned divider = 2u << shift;
        for (unsigned accurate = 0; accurate <= 1; ++accurate) {
            fresh(accurate, divider - 1);
            cpu.cp0.overclock_shift = shift;
            cpu.cp0.regs[CP0_COUNT_REG] = UINT32_MAX;
            cpu.cp0.cycle_count = -2;
            uint64_t before = r4300_cpu_clock(&cpu);
            assert(cp0_cycles_before_event(&cpu.cp0) == divider);
            cp0_step_cycles(&cpu.cp0, 1);
            assert(cpu.cp0.count_phase == 0 && cpu.cp0.regs[CP0_COUNT_REG] == 0);
            assert(cpu.cp0.count_clock == 1001 && cpu.cp0.cycle_count == -1);
            assert(r4300_cpu_clock(&cpu) == before + 1);
            r4300_data_access_cycles_explicit(&cpu, 0xa4040010, 0x04040010, 1, 0);
            assert(cpu.cp0.count_phase == 0 && cpu.cp0.count_clock == 1005);
            assert(r4300_cpu_clock(&cpu) == before + 1 + 8 * (1u << shift));
            if (accurate) {
                cpu.count_write_clock = r4300_cpu_clock(&cpu);
                cpu.count_write_value = 123;
                cpu.count_write_valid = 1;
                cpu.cp0.regs[CP0_COUNT_REG] = 122;
                cp0_step_cycles(&cpu.cp0, divider - 1);
                assert(r4300_read_count(&cpu) == 123);
                cp0_step_cycles(&cpu.cp0, 1);
                assert(r4300_read_count(&cpu) == 123);
            }
            ++cases;
        }
        check_scaled_jit(0, shift);
        check_scaled_jit(1, shift);
        cases += 2;
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
    /* Fill pressure follows the monotonic scheduler clock, not writable or
     * wrapping CP0 COUNT. Keep the native admission bound conservative. */
    assert(R4300_ICACHE_FILL_IDLE_CYCLES <= R4300_ICACHE_FILL_CYCLES);
    assert(R4300_ICACHE_FILL_BUSY_CYCLES <= R4300_ICACHE_FILL_CYCLES);
    struct vi_controller fill_vi={0};struct rdp_core fill_dp={0};
    for(unsigned wrap=0;wrap<2;++wrap)for(unsigned phase=0;phase<2;++phase){
        fresh(1,phase);cpu.cp0.count_clock=wrap?INT64_C(0x100000000)+1000:1000;
        cpu.cp0.regs[CP0_COUNT_REG]=UINT32_MAX-1;cpu.vi=&fill_vi;fill_vi.dp=&fill_dp;
        fill_dp.timing.deadline=cpu.cp0.count_clock;
        assert(r4300_icache_fill_cycles(&cpu)==R4300_ICACHE_FILL_IDLE_CYCLES);++cases;
        fill_dp.timing.deadline=cpu.cp0.count_clock+1;
        assert(r4300_icache_fill_cycles(&cpu)==R4300_ICACHE_FILL_BUSY_CYCLES);++cases;
        fill_vi.dp=NULL;
        assert(r4300_icache_fill_cycles(&cpu)==R4300_ICACHE_FILL_IDLE_CYCLES);++cases;
    }
    r4300_jit_destroy(&cpu);printf("Timing: %u On/Off latency, half-clock, wrap, RCP and pipeline contracts passed.\n",cases);return 0;
}
