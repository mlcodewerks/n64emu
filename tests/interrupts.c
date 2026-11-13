#include "headless.h"
#define main unused_timing_main
#include "../benchmarks/cpu_timing.c"
#undef main

static void fresh(uint32_t count)
{
    r4300_jit_destroy(&cpu);memset(&memory,0,sizeof(memory));memset(&mi,0,sizeof(mi));setup(0);
    cpu.cp0.regs[CP0_COUNT_REG]=count;cpu.cp0.regs[CP0_COMPARE_REG]=count+10000;
    init_interrupt(&cpu.cp0);while(cpu.cp0.q.first)remove_interrupt_event(&cpu.cp0);
    cpu.cp0.count_clock=5000;cpu.cp0.regs[CP0_STATUS_REG]=0;
}
int main(void)
{
    unit_headless();
    unsigned cases=0;
    const int types[]={VI_INT,PI_INT,SI_INT,AI_INT,SP_INT,DP_INT,RSP_DMA_EVT,HW2_INT};
    for(unsigned wrap=0;wrap<2;++wrap)for(unsigned seed=0;seed<32;++seed){
        uint32_t count=wrap?UINT32_MAX-16:100;fresh(count);
        for(unsigned i=0;i<8;++i){unsigned j=(i*5+seed)%8;add_interrupt_event(&cpu.cp0,types[j],3+11*j);}
        add_interrupt_event(&cpu.cp0,COMPARE_INT,10000);
        assert(cpu.cp0.cycle_count==-3 && cpu.cp0.next_interrupt==count+3);
        char saved[2048];memset(saved,0x5a,sizeof(saved));int n=save_eventqueue_infos(&cpu.cp0,saved);
        assert(n>0 && n<(int)sizeof(saved) && saved[n]==0x5a);
        load_eventqueue_infos(&cpu.cp0,saved);
        for(unsigned i=0;i<8;++i){
            assert(get_next_event_type(&cpu.cp0.q)==types[i]);assert(*get_event(&cpu.cp0.q,types[i])==count+3+11*i);
            remove_interrupt_event(&cpu.cp0);++cases;
        }
        assert(get_next_event_type(&cpu.cp0.q)==COMPARE_INT);remove_interrupt_event(&cpu.cp0);
        assert(!cpu.cp0.q.first && cpu.cp0.q.pool.index==0);
    }
    for(unsigned ie=0;ie<2;++ie)for(unsigned exl=0;exl<2;++exl)for(unsigned erl=0;erl<2;++erl)for(unsigned mask=0;mask<2;++mask){
        fresh(500);add_interrupt_event(&cpu.cp0,PI_INT,1);
        cpu.cp0.regs[CP0_STATUS_REG]=(ie?CP0_STATUS_IE:0)|(exl?CP0_STATUS_EXL:0)|(erl?CP0_STATUS_ERL:0)|(mask?CP0_CAUSE_IP2:0);
        r4300_check_interrupt(&cpu,CP0_CAUSE_IP2,1);
        int eligible=ie&&!exl&&!erl&&mask;
        assert(!!get_event(&cpu.cp0.q,CHECK_INT)==eligible);assert(cpu.cp0.regs[CP0_CAUSE_REG]&CP0_CAUSE_IP2);
        if(eligible){
            unsigned size=cpu.cp0.q.pool.index;r4300_check_interrupt(&cpu,CP0_CAUSE_IP2,1);
            assert(cpu.cp0.q.pool.index==size && get_next_event_type(&cpu.cp0.q)==CHECK_INT);
            assert(cpu.cp0.cycle_count==0 && cpu.cp0.next_interrupt==500);
        }
        r4300_check_interrupt(&cpu,CP0_CAUSE_IP2,0);assert(!(cpu.cp0.regs[CP0_CAUSE_REG]&CP0_CAUSE_IP2));++cases;
    }
    for(unsigned delay=0;delay<2;++delay){
        fresh(0);cpu.cp0.regs[CP0_STATUS_REG]=CP0_STATUS_IE|CP0_CAUSE_IP7;
        cpu.instruction_pc.addr=0x80000000+4*delay;cpu.delay_slot=delay;
        raise_maskable_interrupt(&cpu,CP0_CAUSE_IP7);
        assert(cpu.cp0.regs[CP0_STATUS_REG]&CP0_STATUS_EXL);
        assert(cpu.cp0.regs[CP0_EPC_REG]==0x80000000);
        assert(!!(cpu.cp0.regs[CP0_CAUSE_REG]&UINT32_C(0x80000000))==delay);
        assert(cpu.instruction_pc.addr==0x80000180);++cases;
    }
    fresh(0);cpu.irq_writeback_pending=1;cpu.irq_writeback_deadline=12;cpu.retired_ops=11;
    cpu.cp0.regs[CP0_STATUS_REG]=CP0_STATUS_IE|CP0_CAUSE_IP2;cpu.cp0.regs[CP0_CAUSE_REG]=CP0_CAUSE_IP2;
    r4300_finish_irq_writeback(&cpu);assert(!(cpu.cp0.regs[CP0_STATUS_REG]&CP0_STATUS_EXL));
    cpu.retired_ops=12;cpu.delay_slot=1;r4300_finish_irq_writeback(&cpu);assert(cpu.irq_writeback_pending);
    cpu.delay_slot=0;r4300_finish_irq_writeback(&cpu);assert(!cpu.irq_writeback_pending && (cpu.cp0.regs[CP0_STATUS_REG]&CP0_STATUS_EXL));++cases;
    r4300_jit_destroy(&cpu);printf("Interrupts: %u queue/wrap/serialization/mask/delay cases passed.\n",cases);return 0;
}
