#include "support.h"
#include "device/device.h"
#include "device/r4300/interrupt.h"
static unsigned presentations;
void cp0_update_count(struct r4300_core* r){(void)r;}
uint32_t* r4300_cp0_regs(struct cp0* c){return c->regs;}
unsigned* get_event(const struct interrupt_queue* q,int type){(void)q;(void)type;return NULL;}
void add_interrupt_event(struct cp0* c,int type,unsigned delay){(void)c;(void)type;(void)delay;assert(!"unexpected command submission");}
void signal_rcp_interrupt(struct mi_controller* mi,uint32_t irq){assert(irq==MI_INTR_DP);mi->regs[MI_INTR_REG]|=irq;}
void renderer_process_commands(void){assert(!"unexpected command submission");}
void renderer_present(void){++presentations;}
void init_fb(struct fb* f,struct memory* m,struct rdram* r,struct r4300_core* c){memset(f,0,sizeof(*f));f->mem=m;f->rdram=r;f->r4300=c;}
void poweron_fb(struct fb* f){(void)f;}
void protect_framebuffers(struct fb* f){(void)f;assert(!"unexpected framebuffer protection");}
void unprotect_framebuffers(struct fb* f){(void)f;assert(!"unexpected framebuffer protection");}

static void check_sync_full_delay(void)
{
    uint32_t vi[VI_REGS_COUNT] = {
        [VI_STATUS_REG] = 2,
        [VI_WIDTH_REG] = 320,
        [VI_H_START_REG] = (108u << 16) | 748u,
        [VI_V_START_REG] = (34u << 16) | 514u,
        [VI_X_SCALE_REG] = 512,
        [VI_Y_SCALE_REG] = 1024
    };
    struct rdp_core dp = {0};
    assert(rdp_sync_full_delay(&dp) == 1);
    dp.vi_regs = vi;
    assert(rdp_sync_full_delay(&dp) == 320 * 240 * 2);

    /* The fallback pitch is in source pixels and ignores reserved bits. */
    vi[VI_WIDTH_REG] = 0xa0000140u;
    vi[VI_H_START_REG] = 0;
    assert(rdp_sync_full_delay(&dp) == 320 * 240 * 2);

    vi[VI_STATUS_REG] |= 0x40;
    vi[VI_WIDTH_REG] = 640;
    vi[VI_H_START_REG] = (108u << 16) | 748u;
    dp.instruction_timing = 1;
    dp.timing.fb_width = 320;
    assert(rdp_sync_full_delay(&dp) == 320 * 480 * 2);

    vi[VI_V_START_REG] = (514u << 16) | 34u;
    assert(rdp_sync_full_delay(&dp) == 1);
}

int main(void)
{
    unit_headless();static struct r4300_core cpu;static struct rsp_core sp;
    check_sync_full_delay();
    struct mi_controller mi={0};mi.r4300=&cpu;struct rdp_core dp={0};uint32_t value;
    init_rdp(&dp,&sp,&mi,NULL,NULL,&cpu);poweron_rdp(&dp);
    read_dpc_regs(&dp,DPC_STATUS_REG*4,&value);assert(value==DPC_STATUS_START_GCLK);
    for(unsigned bit=0;bit<3;++bit){
        write_dpc_regs(&dp,DPC_STATUS_REG*4,2u<<(2*bit),UINT32_MAX);
        assert(dp.dpc_regs[DPC_STATUS_REG]&(1u<<bit));
        write_dpc_regs(&dp,DPC_STATUS_REG*4,1u<<(2*bit),UINT32_MAX);
        assert(!(dp.dpc_regs[DPC_STATUS_REG]&(1u<<bit)));
    }
    write_dpc_regs(&dp,DPC_STATUS_REG*4,DPC_SET_FREEZE,UINT32_MAX);
    write_dpc_regs(&dp,DPC_START_REG*4,0xab001007,UINT32_MAX);
    write_dpc_regs(&dp,DPC_START_REG*4,0x2000,UINT32_MAX); /* First start is latched. */
    assert(dp.dpc_regs[DPC_START_REG]==0x1000);
    write_dpc_regs(&dp,DPC_END_REG*4,0x1007,UINT32_MAX);
    assert(dp.dpc_regs[DPC_CURRENT_REG]==0x1000 && dp.dpc_regs[DPC_END_REG]==0x1000);
    assert(!(dp.dpc_regs[DPC_STATUS_REG]&DPC_STATUS_START_VALID));
    write_dpc_regs(&dp,DPC_CURRENT_REG*4,0x2000,UINT32_MAX);assert(dp.dpc_regs[DPC_CURRENT_REG]==0x1000);
    dp.do_on_unfreeze=DELAY_DP_INT|DELAY_UPDATESCREEN;
    write_dpc_regs(&dp,DPC_STATUS_REG*4,DPC_CLR_FREEZE,UINT32_MAX);
    assert(presentations==1 && !dp.do_on_unfreeze && (mi.regs[MI_INTR_REG]&MI_INTR_DP));
    cpu.cp0.regs[CP0_COUNT_REG]=UINT32_MAX-2;
    write_dpc_regs(&dp,DPC_STATUS_REG*4,DPC_CLR_CLOCK_CTR,UINT32_MAX);
    cpu.cp0.regs[CP0_COUNT_REG]=3;read_dpc_regs(&dp,DPC_CLOCK_REG*4,&value);assert(value==8);
    dp.dpc_regs[DPC_CLOCK_REG]=0xabc;rdp_restore_dpc_clock(&dp,3);assert(rdp_dpc_clock_value(&dp,6)==0xac0);
    for(unsigned reg=0;reg<4;++reg){write_dps_regs(&dp,reg*4,0x12345678,UINT32_MAX);write_dps_regs(&dp,reg*4,0xab000000,0xff000000);read_dps_regs(&dp,reg*4,&value);assert(value==0xab345678);}
    dp.dpc_regs[DPC_BUFBUSY_REG]=dp.dpc_regs[DPC_PIPEBUSY_REG]=dp.dpc_regs[DPC_TMEM_REG]=99;
    write_dpc_regs(&dp,DPC_STATUS_REG*4,DPC_CLR_CMD_CTR|DPC_CLR_PIPE_CTR|DPC_CLR_TMEM_CTR,UINT32_MAX);
    assert(!dp.dpc_regs[DPC_BUFBUSY_REG] && !dp.dpc_regs[DPC_PIPEBUSY_REG] && !dp.dpc_regs[DPC_TMEM_REG]);
    poweron_rdp(&dp);
    puts("RDP: DPC flags/latches, unfreeze notifications, clock wrap/restore, SyncFull delay, DPS masks passed.");return 0;
}
