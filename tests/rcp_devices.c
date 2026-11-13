/* Exercise real register/DMA devices, with strict mocks only at CPU,
 * renderer and Joybus boundaries. CPU IRQ delivery has its own real suite. */
#include "support.h"
#include "device/device.h"
#include "device/r4300/interrupt.h"
#include "device/dd_controller.h"
static int event_type;static unsigned event_delay,event_count,invalidations,status_changes,width_changes,pif_processes,pif_updates,dma_calls;
void cp0_update_count(struct r4300_core* r){(void)r;}
int64_t* r4300_cp0_cycle_count(struct cp0* c){return &c->cycle_count;}
void dd_update_bm(void* p){(void)p;assert(!"unexpected DD block manager");}
void r4300_check_interrupt(struct r4300_core* r,uint32_t mask,int set){if(set)r->cp0.regs[CP0_CAUSE_REG]|=mask;else r->cp0.regs[CP0_CAUSE_REG]&=~mask;}
void raise_maskable_interrupt(struct r4300_core* r,uint32_t mask){r4300_check_interrupt(r,mask,1);}
void gen_interrupt(struct r4300_core* r){(void)r;assert(!"unexpected immediate CPU interrupt");}
void add_interrupt_event(struct cp0* c,int type,unsigned delay){(void)c;event_type=type;event_delay=delay;++event_count;}
unsigned* get_event(const struct interrupt_queue* q,int type){(void)q;(void)type;return NULL;}
void remove_event(struct interrupt_queue* q,int type){(void)q;assert(type==PI_INT);event_type=0;}
void remove_interrupt_event(struct cp0* c){(void)c;event_type=0;}
unsigned add_random_interrupt_time(struct r4300_core* r){(void)r;return 0;}
void invalidate_r4300_cached_code(struct r4300_core* r,uint32_t address,size_t size){(void)r;assert(address<1024 && size<=64);++invalidations;}
void pre_framebuffer_read(struct fb* f,uint32_t address){(void)f;assert(address==0x100);}
void post_framebuffer_write(struct fb* f,uint32_t address,uint32_t size){(void)f;assert(address==0x100 && size==16);}
void renderer_vi_status_changed(void){++status_changes;}
void renderer_vi_width_changed(void){++width_changes;}
void process_pif_ram(struct pif* p){assert(p->ram);++pif_processes;}
void update_pif_ram(struct pif* p){assert(p->ram);++pif_updates;}
void dd_on_pi_cart_addr_write(struct dd_controller* d,uint32_t a){(void)d;(void)a;assert(!"unexpected DD mapping");}
static unsigned cart_read(void* p,const uint8_t* dram,uint32_t da,uint32_t ca,uint32_t n){(void)p;assert(da==0x100 && ca==0x10000000 && n==16);for(unsigned i=0;i<n;++i)assert(dram[(da+i)^S8]==(uint8_t)i);++dma_calls;return 0;}
static unsigned cart_write(void* p,uint8_t* dram,uint32_t da,uint32_t ca,uint32_t n){(void)p;assert(da==0x100 && ca==0x10000000 && n==16);for(unsigned i=0;i<n;++i)dram[(da+i)^S8]=(uint8_t)i;++dma_calls;return 0;}
static const struct pi_dma_handler cart_io={cart_read,cart_write};
static void get_cart(struct cart* c,struct dd_controller* d,uint32_t a,void** p,const struct pi_dma_handler** h){(void)c;assert(!d && a==0x10000000);*p=NULL;*h=&cart_io;}
int main(void)
{
    unit_headless();static struct r4300_core cpu;struct mi_controller mi;uint32_t value;cpu.cp0.cycle_count=-100000;
    init_mi(&mi,&cpu);poweron_mi(&mi);read_mi_regs(&mi,MI_VERSION_REG*4,&value);assert(value==0x02020102);
    for(unsigned bit=0;bit<6;++bit){
        uint32_t irq=1u<<bit;signal_rcp_interrupt(&mi,irq);assert(!(cpu.cp0.regs[CP0_CAUSE_REG]&CP0_CAUSE_IP2));
        write_mi_regs(&mi,MI_INTR_MASK_REG*4,2u<<(2*bit),UINT32_MAX);assert(mi.regs[MI_INTR_MASK_REG]==irq && (cpu.cp0.regs[CP0_CAUSE_REG]&CP0_CAUSE_IP2));
        clear_rcp_interrupt(&mi,irq);assert(!mi.regs[MI_INTR_REG] && !(cpu.cp0.regs[CP0_CAUSE_REG]&CP0_CAUSE_IP2));
        write_mi_regs(&mi,MI_INTR_MASK_REG*4,1u<<(2*bit),UINT32_MAX);assert(!mi.regs[MI_INTR_MASK_REG]);
    }
    uint32_t dram[256]={0};struct rdram ram={0};ram.dram=dram;ram.dram_size=sizeof(dram);struct ri_controller ri;init_ri(&ri,&ram);poweron_ri(&ri);
    for(unsigned bits=0;bits<8;++bits){ri.regs[RI_ERROR_REG]=bits&1;ri.regs[RI_MODE_REG]=(bits&2)?8:0;ri.regs[RI_SELECT_REG]=(bits&4)?16:0;read_ri_regs(&ri,RI_CURRENT_LOAD_REG*4,&value);assert(value==(6|(bits&1)|((bits&2)?8:0)|((bits&4)?16:0)));}
    struct vi_controller vi;init_vi(&vi,48681812,60,&mi,NULL);poweron_vi(&vi);vi.regs[VI_V_SYNC_REG]=524;vi.regs[VI_H_SYNC_REG]=3093;
    uint64_t sum=0,numerator=UINT64_C(46875000)*525*3094,denominator=UINT64_C(48681812)*2;
    for(unsigned field=1;field<=1000;++field){vi_schedule_vertical_interrupt(&vi);assert(event_type==VI_INT && event_delay==vi.field_ticks);sum+=event_delay;assert(sum==numerator*field/denominator);cpu.cp0.count_clock+=event_delay;}
    vi.field_ticks=1000;vi.field_start_count_clock=0;vi.regs[VI_V_SYNC_REG]=9;cpu.cp0.count_clock=500;read_vi_regs(&vi,VI_CURRENT_REG*4,&value);assert(value==4);cpu.cp0.regs[CP0_COUNT_REG]=UINT32_MAX;read_vi_regs(&vi,VI_CURRENT_REG*4,&value);assert(value==4);
    write_vi_regs(&vi,VI_STATUS_REG*4,3,UINT32_MAX);write_vi_regs(&vi,VI_STATUS_REG*4,3,UINT32_MAX);write_vi_regs(&vi,VI_WIDTH_REG*4,320,UINT32_MAX);assert(status_changes==1 && width_changes==1);
    struct rdp_core dp={0};struct pi_controller pi;init_pi(&pi,get_cart,NULL,NULL,&mi,&ri,&dp);poweron_pi(&pi);
    for(unsigned direction=0;direction<2;++direction){
        write_pi_regs(&pi,PI_STATUS_REG*4,PI_STATUS_RESET,UINT32_MAX);
        write_pi_regs(&pi,PI_DRAM_ADDR_REG*4,0x101,UINT32_MAX);write_pi_regs(&pi,PI_CART_ADDR_REG*4,0x10000001,UINT32_MAX);
        unsigned before=dma_calls;write_pi_regs(&pi,(direction?PI_RD_LEN_REG:PI_WR_LEN_REG)*4,15,UINT32_MAX);
        assert(dma_calls==before+1 && event_type==PI_INT && event_delay>0 && (pi.regs[PI_STATUS_REG]&PI_STATUS_DMA_BUSY));
        assert(pi.regs[PI_DRAM_ADDR_REG]==0x110 && pi.regs[PI_CART_ADDR_REG]==0x10000010);
        write_pi_regs(&pi,PI_WR_LEN_REG*4,15,UINT32_MAX);assert(dma_calls==before+1 && (pi.regs[PI_STATUS_REG]&PI_STATUS_ERROR));
        pi_end_of_dma_event(&pi);assert(!(pi.regs[PI_STATUS_REG]&PI_STATUS_DMA_BUSY) && (mi.regs[MI_INTR_REG]&MI_INTR_PI));
        write_pi_regs(&pi,PI_STATUS_REG*4,PI_STATUS_CLR_INTR,UINT32_MAX);assert(!(mi.regs[MI_INTR_REG]&MI_INTR_PI));
    }
    uint32_t pif_words[16]={0};struct pif pif={0};pif.ram=(uint8_t*)pif_words;struct si_controller si;init_si(&si,1234,&mi,&pif,&ri);poweron_si(&si);
    for(unsigned i=0;i<16;++i)dram[64+i]=0x10203000+i;
    write_si_regs(&si,SI_DRAM_ADDR_REG*4,0x103,UINT32_MAX);write_si_regs(&si,SI_PIF_ADDR_WR64B_REG*4,0x1fc007c0,UINT32_MAX);
    assert(event_type==SI_INT && event_delay==1234 && !pif_processes && pif_words[0]==0);
    unsigned before=event_count;write_si_regs(&si,SI_PIF_ADDR_WR64B_REG*4,0x1fc007c0,UINT32_MAX);assert(event_count==before && (si.regs[SI_STATUS_REG]&SI_STATUS_DMA_ERROR));
    si_end_of_dma_event(&si);assert(pif_processes==1 && (mi.regs[MI_INTR_REG]&MI_INTR_SI));for(unsigned i=0;i<16;++i)assert(pif_words[i]==fromhl(dram[64+i]));
    write_si_regs(&si,SI_STATUS_REG*4,0,UINT32_MAX);assert(!(mi.regs[MI_INTR_REG]&MI_INTR_SI));
    memset(dram+64,0,64);write_si_regs(&si,SI_PIF_ADDR_RD64B_REG*4,0x1fc007c0,UINT32_MAX);si_end_of_dma_event(&si);
    for(unsigned i=0;i<16;++i)assert(dram[64+i]==0x10203000+i);assert(pif_updates==1 && invalidations==2);
    puts("RCP: six MI IRQ masks, RI status, 1000 VI periods/Count independence, PI and SI DMA/busy/ack passed.");return 0;
}
