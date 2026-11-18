/* The Off compromise must change status only, on every CPU backend. */
#define CPU_CACHE_CHECKS_MAIN unused_fp_status_cache_main
#include "../benchmarks/accurate-perf/cpu_cache_checks.c"
#include <fenv.h>

static struct r4300_core baseline;
static unsigned omissions;
static const uint64_t inputs[2][16] = {
    {0,0x80000000,0x3f800000,0xbf800000,0x3f8ccccd,0x40533333,
     1,0x80000001,0x00800000,0x7f7fffff,0x7f800000,0xff800000,
     0x7fbfffff,0x7fc00000,0x40000000,0xc0400000},
    {0,UINT64_C(0x8000000000000000),UINT64_C(0x3ff0000000000000),
     UINT64_C(0xbff0000000000000),UINT64_C(0x3ff199999999999a),
     UINT64_C(0x400a666666666666),1,UINT64_C(0x8000000000000001),
     UINT64_C(0x0010000000000000),UINT64_C(0x7fefffffffffffff),
     UINT64_C(0x7ff0000000000000),UINT64_C(0xfff0000000000000),
     UINT64_C(0x7ff7ffffffffffff),UINT64_C(0x7ff8000000000000),
     UINT64_C(0x4000000000000000),UINT64_C(0xc008000000000000)}};

static void reset_case(uint32_t op, unsigned fmt, unsigned seed,
                       unsigned enabled, unsigned profile)
{
    /* No case writes the TLB. Avoid clearing its 8 MiB LUT per instruction,
     * which would dominate instruction-emulated ARM test runs. */
    memcpy(&cpu,&baseline,offsetof(struct r4300_core,cp0)+offsetof(struct cp0,tlb));
    memcpy(&cpu.cp1,&baseline.cp1,sizeof(cpu)-offsetof(struct r4300_core,cp1));
    code[0]=op; breakloop=false;
    cpu.compat_issue_clock=profile;
    cpu.cp0.regs[CP0_STATUS_REG]=CP0_STATUS_CU1|((seed&16)?CP0_STATUS_FR:0);
    set_fpr_pointers(&cpu.cp1,cpu.cp0.regs[CP0_STATUS_REG]);
    cpu.cp1.fcr31=(seed&3)|(enabled<<7)|((seed&32)?0x1000000:0);
    uint64_t a=inputs[fmt==17][seed&15],b=inputs[fmt==17][(seed*7+3)&15];
    if(fmt>=20) {
        static const uint64_t integers[]={0,1,UINT64_MAX,0x1000001,0x7fffffff,
            UINT64_C(0x80000001),UINT64_C(0x20000000000001),UINT64_C(0x7fffffffffffffff)};
        a=integers[seed&7];
    }
    cpu.cp1.regs[(seed&16)?1:0].dword=a;
    cpu.cp1.regs[2].dword=b;
    cpu.pipeline_load_mask=seed&1?UINT64_C(0xa00000000):UINT64_C(0xa0000000a);
    cpu.pipeline_fpu_result_mask=seed&2?UINT64_C(0x600000000):UINT64_C(0x300000000);
}

static int fallback(struct r4300_core *r,void *block,unsigned index,unsigned phase)
{
    (void)block;(void)index;
    verify(phase==2,"FP fallback phase",index,phase);
    r4300_ops_execute_one(r);
    return 0;
}
static int finish_fp(struct r4300_core *r,uint32_t op,uint64_t bits,unsigned flags)
{
    r4300_ops_fpu_finish(r,op,bits,flags);
    r->regs[0]=0;++r->retired_ops;
    if(r->cp0.cycle_count>=0)gen_interrupt(r);
    return 0;
}
static void compare(const struct snapshot *expected,uint32_t fcr,
                    unsigned seed,unsigned op,int relaxed)
{
    struct snapshot actual=take();
    uint32_t diff=fcr^cpu.cp1.fcr31;
    if(memcmp(expected,&actual,sizeof(actual)) ||
       (diff && (!relaxed || (fcr&0x80) || (diff&~UINT32_C(0x1004)) ||
                 (cpu.cp1.fcr31&~fcr&0x1004)))) {
        fprintf(stderr,"op=%08x seed=%u relaxed=%d FCR=%08x/%08x PC=%x/%x clock=%lld/%lld\n",
            op,seed,relaxed,fcr,cpu.cp1.fcr31,expected->pc,actual.pc,
            (long long)expected->clock,(long long)actual.clock);
        verify(0,"FP policy state/results/clocks/flags",seed,op);
    }
    if(relaxed && diff)++omissions;
}
int main(void)
{
    setup(0);cpu.cache_timing=1;cpu.cp0.regs[CP0_CONFIG_REG]=3;
    cpu.cp0.count_clock=1000;cpu.cp0.cycle_count=-100000;
    for(unsigned i=0;i<512;++i)cpu.icache_tags[i]=1;
    memcpy(&baseline,&cpu,sizeof(cpu));
    struct n64emujit *jit=n64emujit_create();
    verify(jit!=NULL,"FP policy allocation",0,0);
    unsigned cases=0;
    const unsigned formats[]={16,17,20,21},enables[]={0,1,2,31};
    for(unsigned f=0;f<4;++f)for(unsigned fn=0;fn<38;++fn) {
        unsigned fmt=formats[f];
        if(fmt>=20 ? fn!=32&&fn!=33 :
           !(fn<=15 || fn==36 || fn==37 || fn==(fmt==16?33u:32u)))continue;
        uint32_t op=fp_op(fmt,fn);
        struct n64emujit_config config={0};
        config.virtual_pc=0x80000000;config.cache_timing=1;
        config.compat_issue_clock=R4300_ISSUE_RELAXED_FP_STATUS;
        config.commits=&cpu.retired_ops;
        config.fpu_prepare=r4300_ops_jit_fpu_prepare;config.fpu_finish=finish_fp;
        n64emujit_entry entry=n64emujit_compile_optimized(jit,NULL,&op,1,fallback,&config);
        verify(entry!=NULL,"FP policy compile",fmt,fn);
        for(unsigned seed=0;seed<64;++seed)for(unsigned e=0;e<4;++e) {
            reset_case(op,fmt,seed,enables[e],R4300_ISSUE_COMPATIBLE);
            int rounding=fegetround(),exceptions=fetestexcept(FE_ALL_EXCEPT);
            r4300_ops_execute_one(&cpu);
            struct snapshot old=take();uint32_t old_fcr=cpu.cp1.fcr31;
            reset_case(op,fmt,seed,enables[e],R4300_ISSUE_RELAXED_FP_STATUS);
            r4300_ops_execute_one(&cpu);compare(&old,old_fcr,seed,op,1);
            struct snapshot precise=take();uint32_t precise_fcr=cpu.cp1.fcr31;
            reset_case(op,fmt,seed,enables[e],R4300_ISSUE_RELAXED_FP_STATUS);
            entry(&cpu);compare(&precise,precise_fcr,seed,op,0);
            verify(fegetround()==rounding && fetestexcept(FE_ALL_EXCEPT)==exceptions,
                "FP host environment preserved",seed,op);
            ++cases;
        }
    }
    verify(omissions>0,"FP compromise exercised",0,0);
    n64emujit_destroy(jit);
    printf("Off FP status: %u legacy/Off/native comparisons, %u disabled-inexact omissions; results, clocks and enabled exceptions identical.\n",cases,omissions);
    return 0;
}
