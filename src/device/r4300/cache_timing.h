#ifndef N64EMU_R4300_CACHE_TIMING_H
#define N64EMU_R4300_CACHE_TIMING_H
#include "device/memory.h"
#include "device/rcp/rcp.h"
#include "device/rdram.h"
#include "memory_timing.h"
#include <string.h>

/* Shared compatibility hit estimate, including the existing cache handoff. */
enum { R4300_DCACHE_HIT_CYCLES = 1 };
static osal_force_inline unsigned r4300_dcache_hit_cycles(unsigned compatibility)
{
    /* A native D-cache hit uses the instruction's issue clock; dependent
     * consumers pay their separate LDI interlock. Nemu checks both cases. */
    return compatibility ? R4300_DCACHE_HIT_CYCLES : 0;
}
#if defined(__GNUC__) || defined(__clang__)
#define R4300_CACHE_NOINLINE __attribute__((noinline))
#elif defined(_MSC_VER)
#define R4300_CACHE_NOINLINE __declspec(noinline)
#else
#define R4300_CACHE_NOINLINE
#endif

/* Ares CPU cache model: 16 KiB I-cache / 32-byte lines, 8 KiB D-cache /
 * 16-byte lines, direct mapped with virtual indices and physical tags.
 * I-cache fills cost 88 CPU cycles; native D-cache fills cost 41 before VI
 * arbitration. Compatibility fills and writebacks cost 40 CPU cycles.
 * The cache keeps write-back contents. VI arbitration is a coarse surcharge;
 * shared CPU/RSP/RDP bus arbitration remains unmodeled. */
/* Keep the uncommon mapped-address scan out of every load/store handler. */
static R4300_CACHE_NOINLINE int r4300_mapped_access_cached(struct r4300_core* r, uint32_t va)
{
    unsigned int i;
    for (i = 0; i < 32; ++i) {
        const struct tlb_entry* e = &r->cp0.tlb.entries[i];
        if (!e->g && e->asid != (r->cp0.regs[CP0_ENTRYHI_REG] & 255)) continue;
        if (e->v_even && va >= e->start_even && va <= e->end_even) return e->c_even != 2;
        if (e->v_odd && va >= e->start_odd && va <= e->end_odd) return e->c_odd != 2;
    }
    return 0;
}

static osal_force_inline int r4300_access_cached(struct r4300_core* r, uint32_t va)
{
    if ((va & 0xe0000000) == 0x80000000)
        return (r->cp0.regs[CP0_CONFIG_REG] & 7) != 2;
    if ((va & 0xe0000000) == 0xa0000000) return 0;
    return r4300_mapped_access_cached(r, va);
}

/* An older dirty-line writeback owns the memory bus before an IF miss
 * discovered at this issue boundary. Predict it without performing a TLB
 * refill or changing architectural state; the memory instruction still
 * performs its ordinary checks and transfer afterwards. */
static R4300_CACHE_NOINLINE int r4300_instruction_writeback(struct r4300_core* r, uint32_t op)
{
    unsigned major = op >> 26;
    if (major < 0x20) return 0;
    uint64_t effective = (uint64_t)r->regs[(op >> 21) & 31] + (uint64_t)(int64_t)(int16_t)op;
    uint32_t va = (uint32_t)effective, pa = va;
    uint32_t tag = r->dcache_tags[(va >> 4) & 511];
    if ((tag & 3) != 3) return 0;
    unsigned cache_op = (op >> 16) & 31;
    if (major == 0x2f && cache_op == 1) return 1;
    if ((va & UINT32_C(0xc0000000)) != UINT32_C(0x80000000)) {
        uint32_t map = r->cp0.tlb.LUT_r[va >> 12];
        if (!map) return 0;
        pa = (map & UINT32_C(0xfffff000)) | (va & 4095);
    }
    int hit = (tag & ~2u) == ((pa & UINT32_C(0x1ffff000)) | 1);
    if (major == 0x2f) {
        if (cache_op == 0x15 || cache_op == 0x19) return hit;
        return cache_op == 0x0d && !hit;
    }
    return !hit && r4300_access_cached(r, va);
}

/* The VR4300 instruction micro-TLB is a fully-associative two-entry cache of
 * 4 KiB JTLB translations. A hit is free; refilling a valid JTLB translation
 * costs three PClocks. Store the current LUT translation with the VPN so a
 * TLB rewrite cannot leave a stale micro-TLB hit in this boundary model. */
static osal_force_inline unsigned int r4300_itlb_access_cycles(struct r4300_core* r,
                                                               uint32_t vpn,
                                                               uint32_t map)
{
    unsigned int valid = r->pipeline_itlb_valid;
    unsigned int slot;

    if ((valid & 1) && r->pipeline_itlb_vpn[0] == vpn && r->pipeline_itlb_map[0] == map) {
        r->pipeline_itlb_lru = 1;
        return 0;
    }
    if ((valid & 2) && r->pipeline_itlb_vpn[1] == vpn && r->pipeline_itlb_map[1] == map) {
        r->pipeline_itlb_lru = 0;
        return 0;
    }

    if (!(valid & 1)) slot = 0;
    else if (!(valid & 2)) slot = 1;
    else slot = r->pipeline_itlb_lru & 1;

    r->pipeline_itlb_vpn[slot] = vpn;
    r->pipeline_itlb_map[slot] = map;
    r->pipeline_itlb_valid = valid | (1u << slot);
    r->pipeline_itlb_lru = slot ^ 1u;
    return 3;
}

static inline uint64_t r4300_cpu_clock(const struct r4300_core* r)
{
    return (uint64_t)r->cp0.count_clock*2+r->cp0.count_phase;
}
/* Coarse scanout arbitration. Cache-line transfers share the RDRAM bus;
 * uncached reads also pay the row turnaround when VI uses the same bank.
 * This calibrates the common costs, without modeling refresh outliers. */
static inline unsigned r4300_vi_bus_latency(const struct r4300_core* r,uint32_t pa,int line)
{
    const struct vi_controller* vi=r->vi;
    if(r->compat_issue_clock||!vi||(vi->regs[VI_STATUS_REG]&3)<2||!vi->field_ticks)return 0;
    uint32_t start=(vi->regs[VI_V_START_REG]>>16)&0x3ff,end=vi->regs[VI_V_START_REG]&0x3ff;
    uint64_t elapsed=r->cp0.count_clock>vi->field_start_count_clock?(uint64_t)(r->cp0.count_clock-vi->field_start_count_clock):0;
    unsigned scan=(elapsed%vi->field_ticks)*((vi->regs[VI_V_SYNC_REG]&0x3ff)+1)/vi->field_ticks;
    if(scan<start||scan>=end)return 0;
    if(line)return R4300_VI_LINE_CYCLES;
    uint32_t row=(((scan-start)>>1)*(vi->regs[VI_Y_SCALE_REG]&0xfff)+((vi->regs[VI_Y_SCALE_REG]>>16)&0xfff))>>10;
    uint32_t address=(vi->regs[VI_ORIGIN_REG]&0xffffff)+row*vi->regs[VI_WIDTH_REG]*((vi->regs[VI_STATUS_REG]&3)==3?4:2);
    return ((address^pa)&0x700000)?0:R4300_VI_BANK_CYCLES;
}
static inline void r4300_icache_fill_wait(struct r4300_core* r)
{
    if(r->compat_issue_clock||!r->icache_fill_index)return;
    uint64_t now=r4300_cpu_clock(r);
    if(r->icache_fill_end>now)cp0_step_cycles(&r->cp0,(unsigned)(r->icache_fill_end-now));
    r->icache_fill_index=0;
}
static inline void r4300_write_buffer_drain(struct r4300_core* r)
{
    if(r->compat_issue_clock||!r->write_buffer_count)return;
    uint64_t now=r4300_cpu_clock(r),end=r->write_buffer_end[r->write_buffer_count-1];
    if(end>now)cp0_step_cycles(&r->cp0,(unsigned)(end-now));
    r->write_buffer_count=0;
}
static inline void r4300_write_buffer_store(struct r4300_core* r,unsigned cycles)
{
    uint64_t now=r4300_cpu_clock(r);
    unsigned count=r->write_buffer_count,expired=0;
    while(expired<count&&r->write_buffer_end[expired]<=now)++expired;
    if(expired){count-=expired;memmove(r->write_buffer_end,r->write_buffer_end+expired,count*sizeof(uint64_t));}
    if(count==4){
        cp0_step_cycles(&r->cp0,(unsigned)(r->write_buffer_end[0]-now));
        now=r->write_buffer_end[0];
        memmove(r->write_buffer_end,r->write_buffer_end+1,3*sizeof(uint64_t));--count;
    }
    uint64_t start=count?r->write_buffer_end[count-1]:now;
    if(!r->compat_issue_clock&&r->icache_fill_index&&start<r->icache_fill_end)start=r->icache_fill_end;
    r->write_buffer_end[count]=start+cycles;
    r->write_buffer_count=count+1;
}

static inline void r4300_dcache_writeback(struct r4300_core* r, uint32_t va)
{
    r4300_icache_fill_wait(r);
    r4300_write_buffer_drain(r);
    unsigned index = (va >> 4) & 511;
    uint32_t pa = (r->dcache_tags[index] & 0x1ffff000) | (va & 0xff0);
    cp0_step_cycles(&r->cp0, R4300_DCACHE_WRITEBACK_CYCLES);
    if (r->jit) invalidate_r4300_cached_code(r, pa, 16);
    const struct mem_handler* handler = mem_get_handler(r->mem, pa);
    struct rdram* ram = (struct rdram*)handler->opaque;
    unsigned word = rdram_dram_address(pa);
    if (handler->write32 == write_rdram_dram && ram && (uint64_t)word + 4 <= ram->dram_size / 4)
        memcpy(ram->dram + word, r->dcache_words[index], 16);
    else for (unsigned i = 0; i < 4; ++i)
        mem_write32(mem_get_handler(r->mem, pa), pa + i * 4, r->dcache_words[index][i], ~0u);
    r->dcache_tags[index] &= ~2u;
}

static R4300_CACHE_NOINLINE void r4300_dcache_refill(struct r4300_core* r,
                                                   uint32_t va, uint32_t pa)
{
    unsigned index = (va >> 4) & 511;
    if ((r->dcache_tags[index] & 3) == 3) r4300_dcache_writeback(r, va);
    r4300_icache_fill_wait(r);
    r4300_write_buffer_drain(r);
    cp0_step_cycles(&r->cp0,r->compat_issue_clock?R4300_COMPAT_DCACHE_FILL_CYCLES:R4300_DCACHE_FILL_CYCLES+r4300_vi_bus_latency(r,pa,1));
    uint32_t line = pa & 0x1ffffff0;
    const struct mem_handler* handler = mem_get_handler(r->mem, line);
    const struct rdram* ram = (const struct rdram*)handler->opaque;
    unsigned word = rdram_dram_address(line);
    if (handler->read32 == read_rdram_dram && ram && (uint64_t)word + 4 <= ram->dram_size / 4)
        memcpy(r->dcache_words[index], ram->dram + word, 16);
    else for (unsigned i = 0; i < 4; ++i)
        mem_read32(handler, line + i * 4, &r->dcache_words[index][i]);
    r->dcache_tags[index] = (pa & 0x1ffff000) | 1;
}

/* Return cacheability as well as charging the access, so callers do not repeat
 * the segment/config checks or mapped-address TLB scan. Keep the hit path
 * inline; the compiler can retain the larger refill/writeback paths as calls. */
static osal_force_inline int r4300_data_access_cycles_explicit(struct r4300_core* r, uint32_t va,
                                           uint32_t pa, int write, int cached)
{
    uint32_t* tag;
    uint32_t key;
    if (!r->cache_timing) return 0;
    if (!cached) {
        /* CEN64 fault.h: distinguish RCP's posted handoff from RAM reads. */
        uint32_t physical = pa & 0x1fffffff;
        unsigned cycles = R4300_UNCACHED_DATA_CYCLES;
        if (physical >= 0x04000000 && physical < 0x05000000)
            cycles = write ? R4300_RCP_WRITE_CYCLES : R4300_RCP_READ_CYCLES;
        else if (!write && physical < 0x00800000) cycles = R4300_UNCACHED_READ_CYCLES+r4300_vi_bus_latency(r,physical,0);
        if(!r->compat_issue_clock&&write&&physical<0x00800000) {
            /* The pipeline stalls on capacity, not on each posted store.
             * Memory visibility retains the existing in-order write path;
             * later external accesses wait for these bus deadlines. */
            r4300_write_buffer_store(r,cycles);
            return 0;
        }
        /* RCP register transactions use SysAD without occupying RDRAM. */
        if(physical<0x00800000)r4300_icache_fill_wait(r);
        r4300_write_buffer_drain(r);
        cp0_step_cycles(&r->cp0, cycles);
        return 0;
    }
    tag = &r->dcache_tags[(va >> 4) & 511];
    key = (pa & 0x1ffff000) | 1;
    if ((*tag & ~2u) != key) {
        r4300_dcache_refill(r, va, pa);
    } else if (r->compat_issue_clock) cp0_step_cycles(&r->cp0, R4300_DCACHE_HIT_CYCLES);
    if (write) *tag |= 2;
    return 1;
}

static osal_force_inline int r4300_data_access_cycles(struct r4300_core* r, uint32_t va,
                                                    uint32_t pa, int write)
{
    return r4300_data_access_cycles_explicit(r, va, pa, write, r4300_access_cached(r, va));
}

static inline void r4300_icache_discard_shadow(struct r4300_core *r,unsigned index)
{
    uint32_t tag=r->icache_shadow_tags[index];
    if(tag && r->code_page_generation)++r->code_page_generation[(tag&0x1ffff000u)>>12];
    r->icache_shadow_tags[index]=0;
}
static inline void r4300_icache_materialize(struct r4300_core *r,unsigned index)
{
    uint32_t tag=r->icache_tags[index];
    if(!(tag&1) || r->icache_shadow_tags[index]==(tag|1))return;
    uint32_t address=(tag&0x1ffff000u)|((index&127)*32);
    memcpy(r->icache_shadow_words[index],mem_base_u32(r->mem->base,address),32);
    r->icache_shadow_tags[index]=tag|1;
}
static osal_force_inline unsigned int r4300_fetch_cycles_internal(struct r4300_core* r, uint32_t va,int future)
{
    uint32_t pa = va;
    uint32_t* tag;
    uint32_t key;
    unsigned int cycles = 0;
    if (!r->cache_timing) return 0;
    int cached;
    if (r->pc_extension) {
        uint64_t effective = (r->pc_extension & UINT64_C(0xffffffff00000000)) | va;
        if (!r4300_translate_address64(r, effective, 2, &pa, &cached)) return 0;
        cycles += r4300_itlb_access_cycles(r, (uint32_t)(effective >> 12), pa & UINT32_C(0xfffff000));
    } else if ((va & 0xc0000000) != 0x80000000) {
        uint32_t page = r->cp0.tlb.LUT_r[va >> 12];
        if (!page) return 0; /* The dispatcher handles translation faults. */
        cycles += r4300_itlb_access_cycles(r, va >> 12, page & UINT32_C(0xfffff000));
        pa = (page & 0xfffff000) | (va & 0xfff);
    }
    if (!(r->pc_extension ? cached : r4300_access_cached(r, va))) {
        r4300_icache_fill_wait(r);
        r4300_write_buffer_drain(r);
        return cycles + R4300_UNCACHED_FETCH_CYCLES;
    }
    tag = &r->icache_tags[(va >> 5) & 511];
    key = (pa & 0x1ffff000) | 1;
    if(!future&&r->icache_fill_index==((va>>5)&511)+1)r4300_icache_fill_wait(r);
    if (*tag != key) {
        r4300_icache_fill_wait(r);
        if(!future||r->compat_issue_clock)r4300_write_buffer_drain(r);
        r4300_icache_discard_shadow(r,(va>>5)&511);
        *tag = key;
        if(future&&!r->compat_issue_clock){
            uint64_t start=r4300_cpu_clock(r);
            if(r->write_buffer_count&&start<r->write_buffer_end[r->write_buffer_count-1])start=r->write_buffer_end[r->write_buffer_count-1];
            r->icache_fill_end=start+R4300_ICACHE_FILL_CYCLES;r->icache_fill_index=((va>>5)&511)+1;
            return cycles;
        }
        return cycles + R4300_ICACHE_FILL_CYCLES;
    }
    return cycles;
}
static osal_force_inline unsigned r4300_fetch_cycles(struct r4300_core* r,uint32_t va)
{return r4300_fetch_cycles_internal(r,va,0);}
static osal_force_inline void r4300_prefetch_access_cycles(struct r4300_core* r,uint32_t va)
{
    unsigned cycles=r4300_fetch_cycles_internal(r,va,1);
    if(cycles)cp0_step_cycles(&r->cp0,cycles);
}

static osal_force_inline void r4300_fetch_access_cycles(struct r4300_core* r, uint32_t va)
{
    unsigned int cycles = r4300_fetch_cycles(r, va);
    if (cycles) cp0_step_cycles(&r->cp0, cycles);
}

static inline void r4300_cache_operation(struct r4300_core* r, unsigned op,
                                        uint32_t va, uint32_t pa)
{
    uint32_t* tag = (op & 1) ? &r->dcache_tags[(va >> 4) & 511]
                             : &r->icache_tags[(va >> 5) & 511];
    uint32_t key = (pa & 0x1ffff000) | 1;
    int hit = (*tag & ~2u) == key;
    if (!r->cache_timing) return;
    if (!(op&1) && (op==0 || op==8 || (op==0x10 && hit) || op==0x14)) {
        unsigned index=(va>>5)&511;
        r4300_icache_materialize(r,index);
        if(r->jit)invalidate_r4300_cached_code(r,(*tag&0x1ffff000u)|(va&0xfff),32);
        if(op==0x14)r4300_icache_discard_shadow(r,index);
    }
    switch (op) {
    case 0x00: *tag = key & ~1u; break;
    case 0x01:
        if (*tag & 1) {
            if (*tag & 2) { r4300_dcache_writeback(r, va); *tag |= 2; }
            *tag = (key & ~1u) | (*tag & 2);
        }
        break;
    case 0x04: case 0x05:
        r->cp0.regs[CP0_TAGLO_REG] = ((*tag & 0xfffff000) >> 4) |
            ((*tag & 1) ? ((op & 1) ? 0xc0 : 0x80) : 0);
        break;
    case 0x08: case 0x09:
        *tag = ((r->cp0.regs[CP0_TAGLO_REG] << 4) & 0xfffff000) |
            ((r->cp0.regs[CP0_TAGLO_REG] & 0x80) ? 1 : 0) |
            ((op & 1) ? (*tag & 2) : 0);
        if(!(op&1) && r->icache_shadow_tags[(va>>5)&511])
            r->icache_shadow_tags[(va>>5)&511]=*tag|1;
        break;
    case 0x0d:
        if (!hit && (*tag & 3) == 3) r4300_dcache_writeback(r, va);
        *tag = key | (hit ? (*tag & 2) : 0);
        break;
    case 0x10: case 0x11: if (hit) *tag &= ~1u; break;
    case 0x14:
        cp0_step_cycles(&r->cp0, R4300_ICACHE_FILL_CYCLES); *tag = key;
        r4300_icache_materialize(r,(va>>5)&511);
        break;
    case 0x15:
        if (hit) {
            if (*tag & 2) { r4300_dcache_writeback(r, va); *tag |= 2; }
            *tag &= ~1u;
        }
        break;
    case 0x18:
        if (hit) {
            unsigned index = (va >> 5) & 511;
            r4300_icache_materialize(r, index);
            uint32_t line = (pa & UINT32_C(0x1fffffe0));
            if (r->jit) invalidate_r4300_cached_code(r, line, 32);
            for (unsigned i = 0; i < 8; ++i)
                mem_write32(mem_get_handler(r->mem, line), line + i * 4,
                    r->icache_shadow_words[index][i], ~UINT32_C(0));
            cp0_step_cycles(&r->cp0, R4300_ICACHE_FILL_CYCLES);
        }
        break;
    case 0x19:
        if (hit && (*tag & 2)) r4300_dcache_writeback(r, va);
        break;
    }
}
#undef R4300_CACHE_NOINLINE
#endif
