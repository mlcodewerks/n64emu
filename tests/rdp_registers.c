#include "support.h"
#include "device/device.h"
#include "device/r4300/interrupt.h"
#include "device/rcp/rsp/p_rsp/dpc_bridge.h"
static unsigned presentations;
static struct rdp_core* submitting_dp;
static unsigned rsp_cycles, queued_events, queued_delay, next_vi, pending_dp_count;
static int renderer_signals = 1;
void cp0_update_count(struct r4300_core* r){(void)r;}
uint32_t* r4300_cp0_regs(struct cp0* c){return c->regs;}
unsigned int parallelRSPExecutedCycles(void){return rsp_cycles;}
unsigned* get_event(const struct interrupt_queue* q,int type)
{
    (void)q;
    if (!submitting_dp) return NULL;
    if (type == VI_INT && next_vi) return &next_vi;
    if (type == DP_INT && pending_dp_count) return &pending_dp_count;
    return NULL;
}
void add_interrupt_event(struct cp0* c,int type,unsigned delay)
{
    assert(submitting_dp && c == &submitting_dp->mi->r4300->cp0 && type == DP_INT);
    ++queued_events;
    queued_delay = delay;
}
void signal_rcp_interrupt(struct mi_controller* mi,uint32_t irq){assert(irq==MI_INTR_DP);mi->regs[MI_INTR_REG]|=irq;}
void renderer_process_commands(void)
{
    assert(submitting_dp);
    submitting_dp->dpc_regs[DPC_CURRENT_REG] = submitting_dp->dpc_regs[DPC_END_REG];
    if (renderer_signals)
        submitting_dp->mi->regs[MI_INTR_REG] |= MI_INTR_DP;
}
void renderer_present(void){++presentations;}
void init_fb(struct fb* f,struct memory* m,struct rdram* r,struct r4300_core* c){memset(f,0,sizeof(*f));f->mem=m;f->rdram=r;f->r4300=c;}
void poweron_fb(struct fb* f){(void)f;}
void protect_framebuffers(struct fb* f){assert(submitting_dp && f == &submitting_dp->fb);}
void unprotect_framebuffers(struct fb* f){assert(submitting_dp && f == &submitting_dp->fb);}

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

static void check_fullsync_scheduling(void)
{
    enum { NO_MODE_CHANGE = -1 };
    static const struct {
        unsigned from_rsp, cache_timing, decoded, backlog, retrace;
        unsigned cycle_type, image_read;
        int later_image_read;
        unsigned delay;
    } cases[] = {
        {0, 0, 1, 1000,   0, 0, 0, NO_MODE_CHANGE, 947},
        {0, 1, 1, 1000,   0, 0, 0, NO_MODE_CHANGE, 902},
        {1, 0, 1, 1000,   0, 0, 0, NO_MODE_CHANGE, 902},
        {1, 1, 1, 1000,   0, 0, 0, NO_MODE_CHANGE, 902},
        {0, 0, 0,    0,   0, 0, 0, NO_MODE_CHANGE,  84},
        {0, 1, 0,    0,   0, 0, 0, NO_MODE_CHANGE,  80},
        {1, 0, 0,    0,   0, 0, 0, NO_MODE_CHANGE, 147},
        {1, 1, 0,    0,   0, 0, 0, NO_MODE_CHANGE, 140},
        {1, 0, 1,    0,   0, 0, 0, NO_MODE_CHANGE,  82},
        {0, 0, 1, 1000, 920, 0, 0, NO_MODE_CHANGE, 919},
        {1, 0, 1, 1000, 920, 0, 0, NO_MODE_CHANGE, 902},
        {1, 0, 1, 1000,   0, 0, 1, NO_MODE_CHANGE, 947},
        {1, 0, 1, 1000,   0, 1, 0, NO_MODE_CHANGE, 947},
        {1, 0, 1, 1000,   0, 2, 0, NO_MODE_CHANGE, 947},
        {1, 0, 1, 1000,   0, 3, 0, NO_MODE_CHANGE, 947},
        /* A later mode command cannot change the first fence's policy. */
        {1, 0, 1, 1000,   0, 0, 0, 1, 902},
        {1, 0, 1, 1000,   0, 0, 1, 0, 947},
    };
    static struct r4300_core cpu;
    struct vi_controller vi = {0};
    struct mi_controller mi = {0};
    struct rsp_core sp = {0};
    struct rdp_core dp = {0};
    uint32_t commands[6] = {0};
    uint32_t vi_regs[VI_REGS_COUNT] = {
        [VI_WIDTH_REG] = 10,
        [VI_V_START_REG] = 8,
        [VI_Y_SCALE_REG] = 1024,
    };
    cpu.vi = &vi;
    cpu.cp0.count_clock = cpu.cp0.regs[CP0_COUNT_REG] = 100;
    vi.field_ticks = 10000;
    mi.r4300 = &cpu;
    sp.mem = commands;
    sp.slice_start_clock = 120;
    rsp_cycles = 80;
    init_rdp(&dp, &sp, &mi, NULL, NULL, &cpu);
    dp.vi_regs = vi_regs;
    submitting_dp = &dp;
    for (unsigned i = 0; i < sizeof(cases) / sizeof(cases[0]); ++i) {
        poweron_rdp(&dp);
        cpu.cache_timing = cases[i].cache_timing;
        mi.regs[MI_INTR_REG] = 0;
        commands[0] = UINT32_C(0x2f000000) | (cases[i].cycle_type << 20);
        commands[1] = cases[i].image_read ? 0x40u : 0;
        commands[2] = cases[i].decoded ? UINT32_C(0x29000000) : 0;
        unsigned bytes = 4 * sizeof(commands[0]);
        if (cases[i].later_image_read != NO_MODE_CHANGE) {
            commands[4] = UINT32_C(0x2f000000);
            commands[5] = cases[i].later_image_read ? 0x40u : 0;
            bytes = sizeof(commands);
        }
        dp.timing.deadline = cases[i].backlog;
        dp.dpc_regs[DPC_STATUS_REG] |= DPC_STATUS_XBUS_DMEM_DMA;
        next_vi = cases[i].retrace ? 100 + cases[i].retrace : 0;
        queued_events = 0;
        if (cases[i].from_rsp)
            write_rsp_dpc_regs(&dp, DPC_END_REG * 4, bytes, UINT32_MAX);
        else
            write_dpc_regs(&dp, DPC_END_REG * 4, bytes, UINT32_MAX);
        assert(queued_events == 1 && queued_delay == cases[i].delay);
        assert(!(mi.regs[MI_INTR_REG] & MI_INTR_DP));
    }

    /* Alternate-row interlace keeps the allowance. A later SetColorImage
     * can change the next fence's width without changing this one. */
    vi_regs[VI_STATUS_REG] = 0x40;
    vi_regs[VI_WIDTH_REG] = 640;
    next_vi = 0;
    cpu.cache_timing = 0;
    commands[0] = UINT32_C(0x3f10013f); /* 320-pixel color image. */
    commands[1] = 0;
    commands[2] = UINT32_C(0x29000000);
    commands[3] = 0;
    commands[4] = UINT32_C(0x3f10027f); /* 640-pixel color image. */
    commands[5] = 0;
    poweron_rdp(&dp);
    dp.timing.deadline = 1000;
    dp.dpc_regs[DPC_STATUS_REG] |= DPC_STATUS_XBUS_DMEM_DMA;
    queued_events = 0;
    write_rsp_dpc_regs(&dp, DPC_END_REG * 4, sizeof(commands), UINT32_MAX);
    assert(dp.timing.fb_width == 640);
    assert(queued_events == 1 && queued_delay == 947);

    poweron_rdp(&dp);
    dp.timing.deadline = 1000;
    dp.dpc_regs[DPC_STATUS_REG] |= DPC_STATUS_XBUS_DMEM_DMA;
    commands[0] = commands[4];
    queued_events = 0;
    write_rsp_dpc_regs(&dp, DPC_END_REG * 4, sizeof(commands), UINT32_MAX);
    assert(queued_events == 1 && queued_delay == 902);
    submitting_dp = NULL;
}

static void check_native_rsp_fences(void)
{
    static struct r4300_core cpu;
    struct mi_controller mi = {0};
    uint32_t commands[] = {0x29000000u, 0, 0x29000000u, 0, 0x29000000u, 0};
    struct rsp_core sp = {0};
    struct rdp_core dp = {0};
    cpu.cp0.count_clock = cpu.cp0.regs[CP0_COUNT_REG] = 100;
    mi.r4300 = &cpu;
    sp.mem = commands;
    sp.slice_start_clock = 120;
    rsp_cycles = 80;
    init_rdp(&dp, &sp, &mi, NULL, NULL, &cpu);
    poweron_rdp(&dp);
    submitting_dp = &dp;
    renderer_signals = 0;
    queued_events = 0;
    write_rsp_dpc_regs(&dp, DPC_STATUS_REG * 4, DPC_SET_XBUS_DMEM_DMA | DPC_SET_FREEZE, UINT32_MAX);
    write_rsp_dpc_regs(&dp, DPC_END_REG * 4, 8, UINT32_MAX);
    assert(!queued_events && dp.dpc_regs[DPC_CURRENT_REG] == 0);

    sp.slice_start_clock = 124;
    write_rsp_dpc_regs(&dp, DPC_STATUS_REG * 4, DPC_CLR_FREEZE, UINT32_MAX);
    assert(queued_events == 1 && queued_delay == 85);
    assert(!(mi.regs[MI_INTR_REG] & MI_INTR_DP));

    /* The first completion is latched. A later fence must leave it intact. */
    mi.regs[MI_INTR_REG] |= MI_INTR_DP;
    sp.slice_start_clock = 128;
    write_rsp_dpc_regs(&dp, DPC_END_REG * 4, 16, UINT32_MAX);
    assert(queued_events == 2 && queued_delay == 89);
    assert(mi.regs[MI_INTR_REG] & MI_INTR_DP);
    pending_dp_count = 189;
    write_rsp_dpc_regs(&dp, DPC_END_REG * 4, 24, UINT32_MAX);
    assert(queued_events == 2);
    pending_dp_count = 0;

    poweron_rdp(&dp);
    mi.regs[MI_INTR_REG] = 0;
    sp.slice_start_clock = 120;
    commands[0] = 0x24000000u; /* Texture rectangle, split after two words. */
    commands[2] = 0;
    dp.dpc_regs[DPC_STATUS_REG] |= DPC_STATUS_XBUS_DMEM_DMA;
    write_rsp_dpc_regs(&dp, DPC_END_REG * 4, 8, UINT32_MAX);
    assert(queued_events == 2 && dp.timing.pos == 2);
    write_rsp_dpc_regs(&dp, DPC_END_REG * 4, 24, UINT32_MAX);
    assert(queued_events == 3 && queued_delay == 83 && !dp.timing.pos);
    assert(!(mi.regs[MI_INTR_REG] & MI_INTR_DP));
    renderer_signals = 1;
    submitting_dp = NULL;
}

int main(void)
{
    unit_headless();static struct r4300_core cpu;static struct rsp_core sp;
    check_sync_full_delay();
    check_fullsync_scheduling();
    check_native_rsp_fences();
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
    puts("RDP: DPC flags/latches, unfreeze notifications, clock wrap/restore, CPU/RSP SyncFull scheduling, DPS masks passed.");return 0;
}
