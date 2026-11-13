/* Independent architectural vectors, in both timing modes. No ROM needed. */
#include "headless.h"
#define main existing_cpu_timing_main
#include "../benchmarks/cpu_timing.c"
#undef main

#define R(fn) ((1u<<21)|(2u<<16)|(3u<<11)|(fn))
#define S(fn,n) ((2u<<16)|(3u<<11)|((n)<<6)|(fn))
#define I(op,n) (((op)<<26)|(1u<<21)|(3u<<16)|(uint16_t)(n))
struct vector {uint32_t op;uint64_t a,b,result;};
static const struct vector vectors[]={
    {R(0x21),0x7fffffff,1,UINT64_C(0xffffffff80000000)},
    {R(0x23),0,1,UINT64_MAX},{R(0x2d),UINT64_MAX,1,0},
    {R(0x2f),UINT64_C(0x8000000000000000),1,UINT64_C(0x7fffffffffffffff)},
    {R(0x24),UINT64_C(0x123456789abcdef0),UINT64_C(0xff00ff00ff00ff00),UINT64_C(0x120056009a00de00)},
    {R(0x25),UINT64_C(0x123400000000abcd),UINT64_C(0x0000567800000000),UINT64_C(0x123456780000abcd)},
    {R(0x26),UINT64_MAX,0x1234,UINT64_C(0xffffffffffffedcb)},
    {R(0x27),0,0,UINT64_MAX},
    {R(0x2a),UINT64_C(0x8000000000000000),1,1},{R(0x2a),1,UINT64_MAX,0},
    {R(0x2b),UINT64_MAX,1,0},{R(0x2b),0,UINT64_MAX,1},
    {S(0,31),0,1,UINT64_C(0xffffffff80000000)},
    {S(2,31),0,UINT64_C(0xffffffff80000000),1},{S(3,31),0,UINT64_C(0xffffffff80000000),UINT64_MAX},
    {S(0x3c,31),0,1,UINT64_C(0x8000000000000000)},
    {S(0x3e,31),0,UINT64_C(0x8000000000000000),1},
    {S(0x3f,31),0,UINT64_C(0x8000000000000000),UINT64_MAX},
    {R(4),33,1,2},{R(6),33,UINT64_C(0xffffffff80000000),0x40000000},
    {R(7),33,UINT64_C(0xffffffff80000000),UINT64_C(0xffffffffc0000000)},
    {R(0x14),65,1,2},{R(0x16),65,UINT64_C(0x8000000000000000),UINT64_C(0x4000000000000000)},
    {R(0x17),65,UINT64_C(0x8000000000000000),UINT64_C(0xc000000000000000)},
    {I(9,1),0x7fffffff,0,UINT64_C(0xffffffff80000000)},
    {I(0x19,1),UINT64_MAX,0,0},{I(0x19,-1),0,0,UINT64_MAX},
    {I(0xa,-1),UINT64_C(0xfffffffffffffffe),0,1},
    {I(0xb,-1),0,0,1},{I(0xc,0xff00),UINT64_MAX,0,0xff00},
    {I(0xd,0x1234),UINT64_C(0xffffffffffff0000),0,UINT64_C(0xffffffffffff1234)},
    {I(0xe,0xffff),UINT64_MAX,0,UINT64_C(0xffffffffffff0000)},
    {0x3c038000,0,0,UINT64_C(0xffffffff80000000)}
};
static void fresh(unsigned timing)
{
    r4300_jit_destroy(&cpu);memset(&memory,0,sizeof(memory));memset(&mi,0,sizeof(mi));setup(0);
    cpu.cache_timing=timing;cpu.compat_issue_clock=!timing;
    cpu.cp0.regs[CP0_STATUS_REG]=0;cpu.cp0.cycle_count=-100000;
}
int main(void)
{
    unit_headless();
    /* Reuse architectural checks, not the old flat-clock benchmark profile.
     * Current On/Off clock contracts live in timing.c and the cache suite. */
    check_behavior();unsigned cases=0;
    for(unsigned timing=0;timing<2;++timing){
        for(unsigned i=0;i<sizeof(vectors)/sizeof(vectors[0]);++i){
            fresh(timing);code[0]=vectors[i].op;cpu.regs[1]=vectors[i].a;cpu.regs[2]=vectors[i].b;
            r4300_ops_execute_one(&cpu);
            if((uint64_t)cpu.regs[3]!=vectors[i].result)fprintf(stderr,"CPU vector %u timing=%u got=%llx expected=%llx\n",i,timing,(unsigned long long)cpu.regs[3],(unsigned long long)vectors[i].result);
            assert((uint64_t)cpu.regs[3]==vectors[i].result);assert(cpu.regs[0]==0);++cases;
        }
        const unsigned majors[]={4,5,6,7,20,21,22,23};
        for(unsigned k=0;k<8;++k)for(int value=-1;value<=1;++value){
            fresh(timing);unsigned m=majors[k];cpu.regs[1]=value;cpu.regs[2]=0;
            code[0]=(m<<26)|(1<<21)|((m==4||m==5||m==20||m==21)?2<<16:0)|3;
            code[1]=0x24630001;
            int taken=m==4||m==20?value==0:m==5||m==21?value!=0:m==6||m==22?value<=0:value>0;
            r4300_ops_execute_one(&cpu);
            assert(cpu.instruction_pc.addr==(taken?0x80000010:0x80000008));
            assert(cpu.regs[3]==(taken||m<20));++cases;
        }
        for(unsigned indirect=0;indirect<2;++indirect){
            fresh(timing);cpu.regs[1]=(int64_t)(int32_t)0x80000040;
            code[0]=indirect?(1<<21)|(5<<11)|9:0x0c000010;code[1]=0x24630001;
            r4300_ops_execute_one(&cpu);assert(cpu.instruction_pc.addr==0x80000040);
            assert(cpu.regs[indirect?5:31]==(int64_t)(int32_t)0x80000008);assert(cpu.regs[3]==1);++cases;
        }
        const struct {unsigned fn;int64_t a,b,hi,lo;} muldiv[]={
            {0x18,-3,7,-1,-21},{0x19,-1,2,1,-2},
            {0x1a,7,3,1,2},{0x1a,-7,3,-1,-2},{0x1b,-2,2,0,0x7fffffff},
            {0x1c,-3,7,-1,-21},{0x1d,-1,2,1,-2},
            {0x1e,INT64_C(4398046511104),3,1,INT64_C(1466015503701)}
        };
        for(unsigned i=0;i<sizeof(muldiv)/sizeof(muldiv[0]);++i){
            fresh(timing);code[0]=(1<<21)|(2<<16)|muldiv[i].fn;cpu.regs[1]=muldiv[i].a;cpu.regs[2]=muldiv[i].b;
            r4300_ops_execute_one(&cpu);assert(cpu.hi==muldiv[i].hi && cpu.lo==muldiv[i].lo);++cases;
        }
        fresh(timing);code[0]=(1<<21)|(2<<16)|0x21;cpu.regs[1]=7;cpu.regs[2]=8;
        r4300_ops_execute_one(&cpu);assert(cpu.regs[0]==0);++cases;
        fresh(timing);code[0]=R(0x20);cpu.regs[1]=0x7fffffff;cpu.regs[2]=1;cpu.regs[3]=0x55;
        r4300_ops_execute_one(&cpu);assert(cpu.regs[3]==0x55);
        assert((cpu.cp0.regs[CP0_CAUSE_REG]&0x7c)==0x30 && cpu.cp0.regs[CP0_EPC_REG]==0x80000000);++cases;
        fresh(timing);code[0]=0x8c230001;cpu.regs[1]=(int64_t)(int32_t)0x80000000;cpu.regs[3]=0x55;
        r4300_ops_execute_one(&cpu);assert(cpu.regs[3]==0x55);
        assert((cpu.cp0.regs[CP0_CAUSE_REG]&0x7c)==0x10 && cpu.cp0.regs[CP0_BADVADDR_REG]==0x80000001);++cases;
        fresh(timing);code[0]=R(0x2d);cpu.cp0.regs[CP0_STATUS_REG]=CP0_STATUS_MODE_U;
        r4300_ops_execute_one(&cpu);assert((cpu.cp0.regs[CP0_CAUSE_REG]&0x7c)==0x10);++cases;
    }
    r4300_jit_destroy(&cpu);printf("CPU operations: %u independent timing-mode vectors passed.\n",cases);return 0;
}
