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
int main(void)
{
    unit_headless();static struct r4300_core cpu;static struct rsp_core sp;
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
    const uint32_t packet[]={0x29000000,0x12345678};uint8_t saved[30];
    rdp_hle_frozen_append(&dp,packet,sizeof(packet));rdp_hle_frozen_append(&dp,packet,sizeof(packet));
    memset(saved,0xa5,sizeof(saved));assert(!rdp_hle_frozen_save(&dp,saved+1,27));
    assert(saved[0]==0xa5 && saved[1]==0xa5 && saved[29]==0xa5);
    assert(rdp_hle_frozen_save(&dp,saved+1,28) && saved[0]==0xa5 && saved[29]==0xa5);
    rdp_hle_frozen_reset(&dp);assert(rdp_hle_frozen_load(&dp,saved+1,28));
    assert(dp.hle_frozen_bytes==16 && dp.hle_frozen_guest_end==0x1000);
    assert(!memcmp(dp.hle_frozen_words,packet,8) && !memcmp(dp.hle_frozen_words+2,packet,8));
    saved[5]=7;assert(!rdp_hle_frozen_load(&dp,saved+1,28) && dp.hle_frozen_bytes==16);
    saved[1]^=1;assert(!rdp_hle_frozen_load(&dp,saved+1,28) && dp.hle_frozen_bytes==16);
    assert(rdp_hle_frozen_load(&dp,NULL,0) && !dp.hle_frozen_words && !dp.hle_frozen_bytes);
    poweron_rdp(&dp);
    puts("RDP: DPC flags/latches, unfreeze notifications, clock wrap/restore, DPS masks and frozen FIFO serialization/bounds passed.");return 0;
}
