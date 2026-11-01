/* Audio-only device glue: no command decoding, rasterization or presentation.
 * Keep DP registers addressable for CPU/RSP polling and private checkpoints.
 * SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef N64EMU_USF
#error USF renderer stubs must not be linked into a video core.
#endif
#include "core/backend.h"
#include "device/rcp/rdp/rdp_core.h"
#include "device/rcp/rdp/performance_cores.h"
#include "device/rcp/rcp.h"
#include "device/rdram.h"
#include "device/rcp/rsp/hle/rdp_emit_backend.h"
#include "device/rcp/rsp/hle/rdp_emit_hle.h"
#include "device/rcp/rsp/hle/rdp_emit_state.h"
#include "device/rcp/rsp/hle/rdp_emit_rsp.h"
#include "device/rcp/rsp/hle/rdp_emit_naboo.h"
#include <string.h>

void renderer_process_display_list(void) {}
void renderer_process_commands(void) {}
int renderer_open_content(void) { return 1; }
void renderer_close_content(void) {}
void renderer_present(void) {}
void renderer_show_framebuffer(void) {}
void renderer_vi_status_changed(void) {}
void renderer_vi_width_changed(void) {}
void renderer_framebuffer_read(unsigned address) { (void)address; }
void renderer_framebuffer_write(unsigned address,unsigned size) { (void)address; (void)size; }
void renderer_framebuffer_info(void *info) { memset(info,0,6*sizeof(core_framebuffer_info)); }
int renderer_framebuffer_pages(uint8_t *pages,unsigned count) { if(pages)memset(pages,0,count); return 0; }
void renderer_framebuffer_read_range(unsigned address,unsigned size) { (void)address; (void)size; }
void performance_cores_init(void) {}
unsigned performance_cores_count(void) { return 1; }
void performance_cores_set_enabled(bool enabled) { (void)enabled; }
bool performance_core_enter(unsigned worker,struct performance_affinity *state) { (void)worker; state->changed=false; return false; }
bool performance_emulation_enter(struct performance_affinity *state) { state->changed=false; return false; }
void performance_core_leave(struct performance_affinity *state) { (void)state; }

void init_fb(struct fb* fb,struct memory* mem,struct rdram* rdram,struct r4300_core* cpu) {
    memset(fb,0,sizeof(*fb)); fb->mem=mem; fb->rdram=rdram; fb->r4300=cpu;
}
void poweron_fb(struct fb* fb) { (void)fb; }
void read_rdram_fb(void* opaque,uint32_t address,uint32_t* value) {
    struct fb* fb=opaque; read_rdram_dram(fb->rdram,address,value);
}
void write_rdram_fb(void* opaque,uint32_t address,uint32_t value,uint32_t mask) {
    struct fb* fb=opaque; write_rdram_dram(fb->rdram,address,value,mask);
}
void protect_framebuffers(struct fb* fb) { (void)fb; }
void unprotect_framebuffers(struct fb* fb) { (void)fb; }
void pre_framebuffer_read(struct fb* fb,uint32_t address) { (void)fb; (void)address; }
void pre_framebuffer_read_range(struct fb* fb,uint32_t address,uint32_t length) { (void)fb; (void)address; (void)length; }
void post_framebuffer_write(struct fb* fb,uint32_t address,uint32_t length) { (void)fb; (void)address; (void)length; }

void init_rdp(struct rdp_core* dp,struct rsp_core* sp,struct mi_controller* mi,
    struct memory* mem,struct rdram* rdram,struct r4300_core* cpu) {
    memset(dp,0,sizeof(*dp)); dp->sp=sp; dp->mi=mi; init_fb(&dp->fb,mem,rdram,cpu);
}
void poweron_rdp(struct rdp_core* dp) {
    memset(dp->dpc_regs,0,sizeof(dp->dpc_regs)); memset(dp->dps_regs,0,sizeof(dp->dps_regs));
    dp->dpc_regs[DPC_STATUS_REG]=DPC_STATUS_START_GCLK|DPC_STATUS_CBUF_READY;
}
void read_dpc_regs(void* opaque,uint32_t address,uint32_t* value) {
    struct rdp_core* dp=opaque; *value=dp->dpc_regs[dpc_reg(address)];
}
void read_rsp_dpc_regs(void* opaque,uint32_t address,uint32_t* value) { read_dpc_regs(opaque,address,value); }
void write_dpc_regs(void* opaque,uint32_t address,uint32_t value,uint32_t mask) {
    struct rdp_core* dp=opaque; unsigned reg=dpc_reg(address); value&=mask;
    if(reg==DPC_STATUS_REG) {
        for(unsigned bit=0;bit<3;bit++) {
            if(value&(1u<<(bit*2)))dp->dpc_regs[reg]&=~(1u<<bit);
            if(value&(2u<<(bit*2)))dp->dpc_regs[reg]|=1u<<bit;
        }
        if(value&DPC_CLR_CLOCK_CTR)dp->dpc_regs[DPC_CLOCK_REG]=0;
        if(!(dp->dpc_regs[reg]&DPC_STATUS_FREEZE))dp->dpc_regs[DPC_CURRENT_REG]=dp->dpc_regs[DPC_END_REG];
    } else if(reg==DPC_START_REG||reg==DPC_END_REG) {
        dp->dpc_regs[reg]=((dp->dpc_regs[reg]&~mask)|value)&0x00fffff8;
        if(reg==DPC_START_REG||!(dp->dpc_regs[DPC_STATUS_REG]&DPC_STATUS_FREEZE))dp->dpc_regs[DPC_CURRENT_REG]=dp->dpc_regs[reg];
    }
}
void write_rsp_dpc_regs(void* opaque,uint32_t address,uint32_t value,uint32_t mask) { write_dpc_regs(opaque,address,value,mask); }
void read_dps_regs(void* opaque,uint32_t address,uint32_t* value) { struct rdp_core* dp=opaque; *value=dp->dps_regs[dps_reg(address)]; }
void write_dps_regs(void* opaque,uint32_t address,uint32_t value,uint32_t mask) {
    struct rdp_core* dp=opaque; unsigned reg=dps_reg(address); dp->dps_regs[reg]=(dp->dps_regs[reg]&~mask)|(value&mask);
}
void rdp_interrupt_event(void* opaque) { struct rdp_core* dp=opaque; raise_rcp_interrupt(dp->mi,MI_INTR_DP); }
uint32_t rdp_dpc_clock_value(const struct rdp_core* dp,uint32_t count) { (void)count; return dp->dpc_regs[DPC_CLOCK_REG]; }
void rdp_restore_dpc_clock(struct rdp_core* dp,uint32_t count) { (void)dp; (void)count; }
uint32_t rdp_sync_full_delay(const struct rdp_core* dp) { (void)dp; return 0; }
const uint32_t* rdp_hle_command_buffer(uint32_t address) { (void)address; return NULL; }
void rdp_publish_hle_fifo(struct rdp_core* dp,uint32_t base,uint32_t bytes,int xbus) { (void)dp; (void)base; (void)bytes; (void)xbus; }
void rdp_submit_hle(struct rdp_core* dp,const uint32_t* words,uint32_t base,uint32_t bytes,uint32_t cycles) { (void)dp; (void)words; (void)base; (void)bytes; (void)cycles; }
void rdp_time_hle(struct rdp_core* dp,const uint32_t* words,uint32_t count,uint32_t cycles) { (void)dp; (void)words; (void)count; (void)cycles; }
void rdp_hle_frozen_reset(struct rdp_core* dp) { dp->hle_frozen_bytes=0; }
void rdp_hle_frozen_append(struct rdp_core* dp,const uint32_t* words,uint32_t bytes) { (void)dp; (void)words; (void)bytes; }
int rdp_hle_frozen_save(const struct rdp_core* dp,void* data,uint32_t bytes) { (void)dp; memset(data,0,bytes); return 1; }
int rdp_hle_frozen_load(struct rdp_core* dp,const void* data,uint32_t bytes) { (void)data; (void)bytes; rdp_hle_frozen_reset(dp); return 1; }

void rdp_emit_set_backend(const RdpEmitBackend* backend) { (void)backend; }
void rdp_emit_hle_process_dlist(void) {}
void rdp_emit_hle_reset(void) {}
unsigned int rdp_emit_hle_state_size(void) { return 0; }
void rdp_emit_hle_save(void* data) { (void)data; }
void rdp_emit_hle_load(const void* data) { (void)data; }
unsigned int rdp_emit_stream_state_size(void) { return 0; }
unsigned int rdp_emit_stream_legacy_state_size(void) { return 0; }
void rdp_emit_stream_save(void* data) { (void)data; }
void rdp_emit_stream_load(const void* data) { (void)data; }
void rdp_emit_stream_load_legacy(const void* data) { (void)data; }
unsigned int f3d_state_size(void) { return 0; }
void f3d_state_save(void* data) { (void)data; }
void f3d_state_load(const void* data) { (void)data; }
unsigned int n64video_noise_state_size(void) { return 0; }
void n64video_save_noise(void* data) { (void)data; }
void n64video_load_noise(const void* data) { (void)data; }
void rsp_graphics_entry_carry(unsigned int carry) { (void)carry; }
void rsp_tri_factor5_seed_fraction(uint32_t value) { (void)value; }
void rsp_tri_factor5_context(uint32_t values[5]) { memset(values,0,5*sizeof(*values)); }
void rsp_factor5_seed(const uint16_t values[32][8]) { (void)values; }
uint32_t rsp_factor5_get(uint16_t values[32][8]) { (void)values; return 0; }
void naboo_set_phase(unsigned int phase) { (void)phase; }
int naboo_get_phase(unsigned int* phase) { *phase=0; return 0; }
void naboo_set_context(const unsigned int values[7]) { (void)values; }
unsigned int naboo_get_context(unsigned int values[7]) { memset(values,0,7*sizeof(*values)); return 0; }
void naboo_set_packet_cursor(unsigned int cursor) { (void)cursor; }
unsigned int naboo_get_packet_cursor(void) { return 0; }
void naboo_set_clock(unsigned int clock) { (void)clock; }
