
#include "core/backend.h"
#include "main/main.h"
#include "device/memory.h"
#include "device/rcp/rcp.h"
#include "device/rcp/rsp/optional_rsp.h"
#include "device/rcp/rsp/p_rsp/dpc_bridge.h"
#include <stdlib.h>
#include <string.h>
core_controller Controls[4];
renderer_memory gfx_info;
rsp_memory rsp_info;
static void empty_callback(void) {}
void core_bind_backends(void)
{
    gfx_info.RDRAM = (unsigned char *)mem_base_u32(g_mem_base, MM_RDRAM_DRAM);
    gfx_info.DMEM = (unsigned char *)mem_base_u32(g_mem_base, MM_RSP_MEM);
    gfx_info.IMEM = (unsigned char *)mem_base_u32(g_mem_base, MM_RSP_MEM + 0x1000);
    gfx_info.MI_INTR_REG = &(g_dev.mi.regs[MI_INTR_REG]);
    gfx_info.DPC_START_REG = &(g_dev.dp.dpc_regs[DPC_START_REG]);
    gfx_info.DPC_END_REG = &(g_dev.dp.dpc_regs[DPC_END_REG]);
    gfx_info.DPC_CURRENT_REG = &(g_dev.dp.dpc_regs[DPC_CURRENT_REG]);
    gfx_info.DPC_STATUS_REG = &(g_dev.dp.dpc_regs[DPC_STATUS_REG]);
    gfx_info.DPC_CLOCK_REG = &(g_dev.dp.dpc_regs[DPC_CLOCK_REG]);
    gfx_info.DPC_BUFBUSY_REG = &(g_dev.dp.dpc_regs[DPC_BUFBUSY_REG]);
    gfx_info.DPC_PIPEBUSY_REG = &(g_dev.dp.dpc_regs[DPC_PIPEBUSY_REG]);
    gfx_info.DPC_TMEM_REG = &(g_dev.dp.dpc_regs[DPC_TMEM_REG]);
    gfx_info.VI_STATUS_REG = &(g_dev.vi.regs[VI_STATUS_REG]);
    gfx_info.VI_ORIGIN_REG = &(g_dev.vi.regs[VI_ORIGIN_REG]);
    gfx_info.VI_WIDTH_REG = &(g_dev.vi.regs[VI_WIDTH_REG]);
    gfx_info.VI_INTR_REG = &(g_dev.vi.regs[VI_V_INTR_REG]);
    gfx_info.VI_V_CURRENT_LINE_REG = &(g_dev.vi.regs[VI_CURRENT_REG]);
    gfx_info.VI_TIMING_REG = &(g_dev.vi.regs[VI_BURST_REG]);
    gfx_info.VI_V_SYNC_REG = &(g_dev.vi.regs[VI_V_SYNC_REG]);
    gfx_info.VI_H_SYNC_REG = &(g_dev.vi.regs[VI_H_SYNC_REG]);
    gfx_info.VI_LEAP_REG = &(g_dev.vi.regs[VI_LEAP_REG]);
    gfx_info.VI_H_START_REG = &(g_dev.vi.regs[VI_H_START_REG]);
    gfx_info.VI_V_START_REG = &(g_dev.vi.regs[VI_V_START_REG]);
    gfx_info.VI_V_BURST_REG = &(g_dev.vi.regs[VI_V_BURST_REG]);
    gfx_info.VI_X_SCALE_REG = &(g_dev.vi.regs[VI_X_SCALE_REG]);
    gfx_info.VI_Y_SCALE_REG = &(g_dev.vi.regs[VI_Y_SCALE_REG]);
    gfx_info.CheckInterrupts = empty_callback;

    gfx_info.SP_STATUS_REG = &g_dev.sp.regs[SP_STATUS_REG];
    gfx_info.RDRAM_SIZE = (unsigned int *)&g_dev.rdram.dram_size;

    /* fill in the rsp_memory data structure */
    rsp_info.RDRAM = (unsigned char *)mem_base_u32(g_mem_base, MM_RDRAM_DRAM);
    rsp_info.DMEM = (unsigned char *)mem_base_u32(g_mem_base, MM_RSP_MEM);
    rsp_info.IMEM = (unsigned char *)mem_base_u32(g_mem_base, MM_RSP_MEM + 0x1000);
    rsp_info.MI_INTR_REG = &g_dev.mi.regs[MI_INTR_REG];
    rsp_info.SP_MEM_ADDR_REG = &g_dev.sp.regs[SP_MEM_ADDR_REG];
    rsp_info.SP_DRAM_ADDR_REG = &g_dev.sp.regs[SP_DRAM_ADDR_REG];
    rsp_info.SP_RD_LEN_REG = &g_dev.sp.regs[SP_RD_LEN_REG];
    rsp_info.SP_WR_LEN_REG = &g_dev.sp.regs[SP_WR_LEN_REG];
    rsp_info.SP_STATUS_REG = &g_dev.sp.regs[SP_STATUS_REG];
    rsp_info.SP_DMA_FULL_REG = &g_dev.sp.regs[SP_DMA_FULL_REG];
    rsp_info.SP_DMA_BUSY_REG = &g_dev.sp.regs[SP_DMA_BUSY_REG];
    rsp_info.SP_PC_REG = &g_dev.sp.regs2[SP_PC_REG];
    rsp_info.SP_SEMAPHORE_REG = &g_dev.sp.regs[SP_SEMAPHORE_REG];
    rsp_info.DPC_START_REG = &g_dev.dp.dpc_regs[DPC_START_REG];
    rsp_info.DPC_END_REG = &g_dev.dp.dpc_regs[DPC_END_REG];
    rsp_info.DPC_CURRENT_REG = &g_dev.dp.dpc_regs[DPC_CURRENT_REG];
    rsp_info.DPC_STATUS_REG = &g_dev.dp.dpc_regs[DPC_STATUS_REG];
    rsp_info.DPC_CLOCK_REG = &g_dev.dp.dpc_regs[DPC_CLOCK_REG];
    rsp_info.DPC_BUFBUSY_REG = &g_dev.dp.dpc_regs[DPC_BUFBUSY_REG];
    rsp_info.DPC_PIPEBUSY_REG = &g_dev.dp.dpc_regs[DPC_PIPEBUSY_REG];
    rsp_info.DPC_TMEM_REG = &g_dev.dp.dpc_regs[DPC_TMEM_REG];
    rsp_info.CheckInterrupts = empty_callback;

    native_rsp_init(rsp_info);
    optional_rsp_init(rsp_info);
    parallelRSPSetDpcCallbacks(&g_dev.dp, read_rsp_dpc_regs, write_rsp_dpc_regs);
    parallelRSPSetDmaCallbacks(&g_dev.sp, rsp_dma_read_memory, rsp_dma_write_memory);
    core_input_init();
}
