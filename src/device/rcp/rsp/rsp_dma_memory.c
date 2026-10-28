/* Shared memory-coherence callbacks for CPU- and RSP-initiated DMA. */
#include "rsp_core.h"
#include "device/r4300/r4300_core.h"
#include "device/rdram.h"
#include "device/rcp/rcp.h"
#include "device/rcp/rdp/rdp_core.h"
#ifdef __LIBRETRO__
#include "optional_rsp.h"
#endif

void rsp_dma_read_memory(void* opaque, uint32_t address, uint32_t length)
{
    struct rsp_core* sp = (struct rsp_core*)opaque;
    address &= 0xffffff;
    while (length) {
        uint32_t size = length < 0x1000000 - address ? length : 0x1000000 - address;
        uint32_t valid = address < sp->ri->rdram->dram_size ? (uint32_t)sp->ri->rdram->dram_size-address : 0;
        if(valid>size)valid=size;
        if(valid)pre_framebuffer_read_range(&sp->dp->fb, address, valid);
        length -= size; address = 0;
    }
}

void rsp_dma_write_memory(void* opaque, uint32_t address, uint32_t length)
{
    struct rsp_core* sp = (struct rsp_core*)opaque;
    address &= 0xffffff;
    while (length) {
        uint32_t size = length < 0x1000000 - address ? length : 0x1000000 - address;
        uint32_t valid = address < sp->ri->rdram->dram_size ? (uint32_t)sp->ri->rdram->dram_size-address : 0;
        if(valid>size)valid=size;
#ifdef __LIBRETRO__
        if(valid)optional_rsp_timing_dma_write(address, valid);
#endif
        if(valid) {
            invalidate_r4300_cached_code(sp->mi->r4300, address, valid);
            post_framebuffer_write(&sp->dp->fb, address, valid);
        }
        length -= size; address = 0;
    }
}
