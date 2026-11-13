#ifndef N64EMU_R4300_INTERP_POLL_H
#define N64EMU_R4300_INTERP_POLL_H

#include "timing.h"
#include "cpu_ops.h"
#include "device/rdram.h"
#include <string.h>

/* Two-block RAM flag waits: LUI/ADDIU/LBU/ANDI/BEQ/NOP, followed by
 * LUI/LW/BEQ/NOP. Only unchanged iterations can be elided; detailed timing
 * additionally requires every instruction/data access to hit its cache.
 * The plan is decoded interpreter data; it is rebuilt after invalidation. */
struct interp_ram_poll {
    uint32_t pc, target, byte_address, word_address, mask;
    int64_t base_value;
    unsigned char base_reg, byte_reg, mask_reg, word_reg;
};

static int interp_ram_poll_decode(struct interp_ram_poll* p, uint32_t pc,
    const uint32_t* first, uint32_t target, const uint32_t* second)
{
    unsigned a = (first[0] >> 16) & 31;
    unsigned b = (first[2] >> 16) & 31;
    unsigned c = (first[3] >> 16) & 31;
    unsigned d = (second[0] >> 16) & 31;
    if ((pc & UINT32_C(0xe0000000)) != UINT32_C(0x80000000) ||
        (pc >> 12) != (target >> 12) || (pc & 4095) > 4096 - 24 ||
        (target & 4095) > 4096 - 16 || !a || !b || !c || !d ||
        a == b || a == c || a == d || b == c || b == d || c == d)
        return 0;
    if ((first[0] & UINT32_C(0xffe00000)) != UINT32_C(0x3c000000) ||
        (first[1] & UINT32_C(0xffff0000)) != (UINT32_C(0x24000000) | a << 21 | a << 16) ||
        (first[2] & UINT32_C(0xffe00000)) != (UINT32_C(0x90000000) | a << 21) ||
        (first[3] & UINT32_C(0xffe00000)) != (UINT32_C(0x30000000) | b << 21) ||
        (first[4] & UINT32_C(0xffff0000)) != (UINT32_C(0x10000000) | c << 21) ||
        first[5] ||
        pc + 20 + (int32_t)(int16_t)first[4] * 4 != target ||
        (second[0] & UINT32_C(0xffe00000)) != UINT32_C(0x3c000000) ||
        (second[1] & UINT32_C(0xffff0000)) != (UINT32_C(0x8c000000) | d << 21 | d << 16) ||
        (second[2] & UINT32_C(0xffff0000)) != (UINT32_C(0x10000000) | d << 21) ||
        target + 12 + (int32_t)(int16_t)second[2] * 4 != pc || second[3])
        return 0;
    p->pc = pc; p->target = target;
    p->base_value = (int64_t)(int32_t)((first[0] & 65535) << 16);
    p->base_value = (int64_t)(int32_t)((uint32_t)p->base_value + (int32_t)(int16_t)first[1]);
    p->byte_address = (uint32_t)p->base_value + (int32_t)(int16_t)first[2];
    p->word_address = ((second[0] & 65535) << 16) + (int32_t)(int16_t)second[1];
    p->mask = first[3] & 65535;
    p->base_reg = a; p->byte_reg = b; p->mask_reg = c; p->word_reg = d;
    return 1;
}

static int interp_ram_poll_fetch_hits(const struct r4300_core* r, uint32_t pc, unsigned n)
{
    for (unsigned i = 0; i < n; ++i, pc += 4)
        if (r->icache_tags[(pc >> 5) & 511] != ((pc & UINT32_C(0x1ffff000)) | 1))
            return 0;
    return 1;
}

static int interp_ram_poll_read(const struct r4300_core* r, uint32_t address, uint32_t* word)
{
    uint32_t physical = address & UINT32_C(0x1fffffff);
    if ((address & UINT32_C(0xe0000000)) != UINT32_C(0x80000000) ||
        physical >= UINT32_C(0x800000)) return 0;
    const struct mem_handler* h = &r->mem->handlers[physical >> 16];
    if (h->read32 != read_rdram_dram || !h->opaque ||
        physical >= ((const struct rdram*)h->opaque)->dram_size) return 0;
    if (!r->cache_timing) {
        *word = ((const struct rdram*)h->opaque)->dram[physical >> 2];
        return 1;
    }
    unsigned index = (address >> 4) & 511;
    if ((r->dcache_tags[index] & ~2u) != ((physical & UINT32_C(0x1ffff000)) | 1)) return 0;
    *word = r->dcache_words[index][(physical >> 2) & 3];
    return 1;
}

static int interp_ram_poll_advance(struct r4300_core* r, const struct interp_ram_poll* p)
{
    uint32_t byte, word;
    uint32_t status = r->cp0.regs[CP0_STATUS_REG];
    if ((r->cache_timing && (r->cp0.regs[CP0_CONFIG_REG] & 7) == 2) ||
        r->delay_slot || r->skip_jump || r->cp0.cycle_count >= -11 ||
        ((status & CP0_STATUS_MODE_MASK) && !(status & (CP0_STATUS_EXL | CP0_STATUS_ERL))) ||
        r->pipeline_load_mask || r->pipeline_cached_store ||
        r->pipeline_fpu_result_mask || r->pipeline_fpu_cc ||
        r->regs[p->base_reg] != p->base_value || r->regs[p->mask_reg] || r->regs[p->word_reg] ||
        (p->word_address & 3) ||
        (r->cache_timing && (!interp_ram_poll_fetch_hits(r, p->pc, 6) ||
                            !interp_ram_poll_fetch_hits(r, p->target, 4))) ||
        !interp_ram_poll_read(r, p->byte_address, &byte) ||
        !interp_ram_poll_read(r, p->word_address, &word))
        return 0;
    byte = (byte >> ((3 - (p->byte_address & 3)) * 8)) & 255;
    if ((byte & p->mask) || word || r->regs[p->byte_reg] != byte) return 0;

    /* Ten one-cycle instructions and two D-cache hits. In full timing the
     * ANDI and second BEQ each need a real LDI clock. Compatibility modes
     * absorb both interlocks in their issue reservation. Both NOP slots
     * leave pipeline history empty. Elide
     * whole iterations strictly before the next event, then execute the
     * final iteration normally for the exact PC, delay slot and phase.
     * Without detailed timing the ROM-facing compatibility envelope still
     * reserves the issue clocks, while reads bypass stale D-cache contents. */
    const unsigned reservation = r4300_issue_reservation(r->compat_issue_clock);
    const unsigned cycles = 10 * (1 + reservation) +
        (r->cache_timing ? 2 * (r4300_dcache_hit_cycles(reservation) + !reservation) : 2 * R4300_OFF_RAM_CYCLES);
    uint64_t iterations = (uint64_t)(-(r->cp0.cycle_count + 1)) / (cycles / 2);
    if (iterations > UINT32_MAX / cycles) iterations = UINT32_MAX / cycles;
    if (!iterations) return 0;
    cp0_step_cycles(&r->cp0, (unsigned)iterations * cycles);
    r->retired_ops+=iterations*10;
    r->cp0.last_addr = p->pc;
    return 1;
}

/* A stable RAM load followed by a zero-test backedge and NOP. An optional
 * LUI establishes its base on every iteration. The plan contains guest words,
 * never a ROM identity, and every skipped iteration ends at the same state. */
struct interp_simple_poll {uint32_t pc,count,words[4];};
static int interp_simple_poll_decode(struct interp_simple_poll* p,uint32_t pc,const uint32_t* words,unsigned count)
{
    if((count!=3 && count!=4) || (pc&UINT32_C(0xe0000000))!=UINT32_C(0x80000000) ||
        (pc&4095)>4096-count*4 || words[count-1])return 0;
    uint32_t load=words[count-3],branch=words[count-2];
    unsigned major=load>>26,base=(load>>21)&31,rt=(load>>16)&31;
    if(!rt || rt==base || (major!=32 && major!=33 && major!=35 && major!=36 && major!=37 && major!=39) ||
        ((branch>>26)!=4 && (branch>>26)!=5) || (int16_t)branch!=-(int)(count-1) ||
        !((((branch>>21)&31)==rt && !((branch>>16)&31)) ||
          (!((branch>>21)&31) && ((branch>>16)&31)==rt)))return 0;
    if(count==4 && ((words[0]&UINT32_C(0xffe00000))!=UINT32_C(0x3c000000) ||
        !base || ((words[0]>>16)&31)!=base))return 0;
    p->pc=pc;p->count=count;memcpy(p->words,words,count*4);return 1;
}
static const uint32_t* interp_simple_poll_ram(const struct r4300_core* r,uint32_t address,unsigned words)
{
    uint32_t physical=address&UINT32_C(0x1fffffff);
    if((address&UINT32_C(0xe0000000))!=UINT32_C(0x80000000) || (address&3) ||
        physical>=UINT32_C(0x800000) || words*4>UINT32_C(0x800000)-physical ||
        (physical>>16)!=((physical+words*4-1)>>16))return NULL;
    const struct mem_handler* h=&r->mem->handlers[physical>>16];
    if(h->read32!=read_rdram_dram || !h->opaque || words*4>((const struct rdram*)h->opaque)->dram_size ||
        physical>((const struct rdram*)h->opaque)->dram_size-words*4)return NULL;
    return ((const struct rdram*)h->opaque)->dram+(physical>>2);
}
static int interp_simple_poll_advance(struct r4300_core* r,const struct interp_simple_poll* p)
{
    uint32_t status=r->cp0.regs[CP0_STATUS_REG];
    if(breakloop || r->stop || r->delay_slot || r->skip_jump || r->regs[0] ||
        r->instruction_pc.addr!=p->pc || r->cp0.last_addr!=p->pc || r->cp0.cycle_count>=0 || r->cp0.count_phase>1 ||
        ((status&CP0_STATUS_MODE_MASK) && !(status&(CP0_STATUS_EXL|CP0_STATUS_ERL))) ||
        r->pipeline_load_mask || r->pipeline_fpu_result_mask || r->pipeline_fpu_cc || r->pipeline_cached_store)return 0;
    const uint32_t* code=interp_simple_poll_ram(r,p->pc,p->count);
    if(!code || memcmp(code,p->words,p->count*4))return 0;
    uint32_t load=p->words[p->count-3];unsigned major=load>>26,base=(load>>21)&31,rt=(load>>16)&31;
    if(p->count==4 && r->regs[base]!=(int64_t)(int32_t)(p->words[0]<<16))return 0;
    uint32_t address=(uint32_t)r->regs[base]+(int32_t)(int16_t)load;
    unsigned size=major==32 || major==36?1:major==33 || major==37?2:4;
    if(address&(size-1))return 0;
    int cached=r->cache_timing && (r->cp0.regs[CP0_CONFIG_REG]&7)!=2;
    uint32_t word;
    if(cached) {
        if(!interp_ram_poll_fetch_hits(r,p->pc,p->count) || !interp_ram_poll_read(r,address,&word))return 0;
    } else {
        const uint32_t* data=interp_simple_poll_ram(r,address&~3u,1);if(!data)return 0;word=*data;
    }
    int64_t value;
    if(size==1){word=(word>>((3-(address&3))*8))&255;value=major==32?(int64_t)(int8_t)word:word;}
    else if(size==2){word=(word>>((2-(address&2))*8))&65535;value=major==33?(int64_t)(int16_t)word:word;}
    else value=major==35?(int64_t)(int32_t)word:(int64_t)word;
    if(r->regs[rt]!=value || ((p->words[p->count-2]>>26)==4 ? value!=0 : value==0))return 0;
    const unsigned reservation=r4300_issue_reservation(r->compat_issue_clock);
    const unsigned clocks=p->count*(1+reservation+(r->cache_timing && !cached?40:0))+
        (r->cache_timing?(cached?r4300_dcache_hit_cycles(reservation):31)+!reservation:R4300_OFF_RAM_CYCLES);
    // COUNT advances on either half-clock phase. Retain the final partial
    // iteration for ordinary execution so interrupts keep their exact prefix.
    uint64_t budget=(uint64_t)(-(r->cp0.cycle_count+1))*2+(1-r->cp0.count_phase);
    uint64_t iterations=budget/clocks;
    if(iterations>(UINT32_MAX-1)/clocks)iterations=(UINT32_MAX-1)/clocks;
    if(!iterations)return 0;
    cp0_step_cycles(&r->cp0,(unsigned)iterations*clocks);r->retired_ops+=iterations*p->count;r->cp0.last_addr=p->pc;return 1;
}

#endif
