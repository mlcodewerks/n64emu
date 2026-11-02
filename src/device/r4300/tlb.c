

#include "tlb.h"
#include "fastmem.h"
#include <core/frontend.h>

#include "core/types.h"
#include "device/r4300/r4300_core.h"
#include "device/rdram.h"

#include <assert.h>
#include <string.h>

/* Segment checks precede translation. EXL/ERL select kernel addressing;
 * otherwise KSU selects both the accessible segments and the X enable. */
int r4300_address_valid(struct r4300_core* r, uint64_t address, int w)
{
    uint32_t status = r->cp0.regs[CP0_STATUS_REG];
    unsigned mode = status & (CP0_STATUS_EXL | CP0_STATUS_ERL) ? 0 : (status >> 3) & 3;
    uint32_t x = mode == 0 ? CP0_STATUS_KX : mode == 1 ? CP0_STATUS_SX : CP0_STATUS_UX;
    int canonical = address == (uint64_t)(int64_t)(int32_t)address;
    int valid;
    if (canonical) {
        uint32_t a = (uint32_t)address;
        valid = a < 0x80000000u || mode == 0 ||
            (mode == 1 && a >= 0xc0000000u && a < 0xe0000000u);
    } else {
        unsigned region = address >> 62;
        valid = (status & x) != 0;
        if (region == 2)
            valid &= mode == 0 && !(address & UINT64_C(0x07ffffff00000000));
        else {
            valid &= !(address & UINT64_C(0x3fffff0000000000));
            valid &= region == 0 || (region == 1 && mode <= 1) || (region == 3 && mode == 0);
            if (region == 3 && address >= UINT64_C(0xc00000ff80000000)) valid = 0;
        }
    }
    if (valid) return 1;
    cp0_set_fault_address(r, address);
    r->cp0.regs[CP0_CAUSE_REG] = (r->cp0.regs[CP0_CAUSE_REG] & ~UINT32_C(0x3000007c)) |
        (w == 1 ? CP0_CAUSE_EXCCODE_ADES : CP0_CAUSE_EXCCODE_ADEL);
    exception_general(r);
    return 0;
}

int r4300_translate_address64(struct r4300_core* r, uint64_t address, int w,
                            uint32_t* physical, int* cached)
{
    if ((address >> 62) == 2) {
        *physical = (uint32_t)address;
        *cached = ((address >> 59) & 7) != 2;
        return 1;
    }
    uint64_t hi = address & UINT64_C(0xc00000ffffffe000);
    unsigned asid = r->cp0.regs[CP0_ENTRYHI_REG] & 255;
    int matched = 0, modified = 0;
    for (unsigned i = 0; i < 32; ++i) {
        const struct tlb_entry* e = &r->cp0.tlb.entries[i];
        uint64_t mask = (uint64_t)(unsigned short)e->mask << 13;
        uint64_t entry = ((uint64_t)r->cp0.tlb_entryhi_hi[i] << 32) | ((uint64_t)e->vpn2 << 13);
        if ((hi & ~mask) != (entry & ~mask) || (!e->g && e->asid != asid)) continue;
        matched = 1;
        unsigned page_size = ((unsigned short)e->mask + 1u) << 12;
        int odd = (address & page_size) != 0;
        int valid = odd ? e->v_odd : e->v_even;
        modified = valid && w == 1 && !(odd ? e->d_odd : e->d_even);
        if (!valid || modified) break;
        *physical = ((odd ? e->pfn_odd : e->pfn_even) << 12 & ~(page_size - 1)) |
            ((uint32_t)address & (page_size - 1));
        *cached = (odd ? e->c_odd : e->c_even) != 2;
        return 1;
    }
    int nested = (r->cp0.regs[CP0_STATUS_REG] & CP0_STATUS_EXL) != 0;
    r->cp0.regs[CP0_CAUSE_REG] = (r->cp0.regs[CP0_CAUSE_REG] & ~UINT32_C(0x3000007c)) |
        (modified ? CP0_CAUSE_EXCCODE_MOD : w == 1 ? CP0_CAUSE_EXCCODE_TLBS : CP0_CAUSE_EXCCODE_TLBL);
    cp0_set_fault_address(r, address);
    r->cp0.regs[CP0_ENTRYHI_REG] = (uint32_t)hi | asid;
    r->cp0.regs_hi[CP0_ENTRYHI_REG] = (uint32_t)(hi >> 32);
    /* Data helpers run after the PC increment; fetches run at the fault PC. */
    if (w != 2 && !nested) r->instruction_pc.addr -= 4;
    exception_general(r);
    if (!nested && !matched)
        generic_jump_to(r, (r->cp0.regs[CP0_STATUS_REG] & CP0_STATUS_BEV) ?
            UINT32_C(0xbfc00280) : UINT32_C(0x80000080));
    return 0;
}

void poweron_tlb(struct tlb* tlb)
{
    /* clear TLB entries */
    memset(tlb->entries, 0, 32 * sizeof(tlb->entries[0]));
    memset(tlb->LUT_r, 0, 0x100000 * sizeof(tlb->LUT_r[0]));
    memset(tlb->LUT_w, 0, 0x100000 * sizeof(tlb->LUT_w[0]));
    if (tlb->fastmem) r4300_fastmem_refresh(tlb->fastmem);
}

void tlb_unmap(struct tlb* tlb, size_t entry)
{
    uint64_t i; /* The last 4 KiB page ends at UINT32_MAX; do not wrap to zero. */
    const struct tlb_entry* e;

    assert(entry < 32);
    e = &tlb->entries[entry];

    if (e->v_even)
    {
        for (i=e->start_even; i<e->end_even; i += 0x1000)
        {
            tlb->LUT_r[i>>12] = 0;
            if (e->d_even) tlb->LUT_w[i>>12] = 0;
            if (tlb->fastmem) r4300_fastmem_tlb_page(tlb, (uint32_t)(i >> 12));
        }
    }

    if (e->v_odd)
    {
        for (i=e->start_odd; i<e->end_odd; i += 0x1000)
        {
            tlb->LUT_r[i>>12] = 0;
            if (e->d_odd) tlb->LUT_w[i>>12] = 0;
            if (tlb->fastmem) r4300_fastmem_tlb_page(tlb, (uint32_t)(i >> 12));
        }
    }
}

void tlb_map(struct tlb* tlb, size_t entry)
{
    uint64_t i;
    const struct tlb_entry* e;

    assert(entry < 32);
    e = &tlb->entries[entry];

    if (e->v_even)
    {
        if (e->start_even < e->end_even &&
            !(e->start_even >= 0x80000000 && e->end_even < 0xC0000000) &&
            e->phys_even < 0x20000000)
        {
            for (i=e->start_even;i<e->end_even;i+=0x1000)
            {
                tlb->LUT_r[i>>12] = UINT32_C(0x80000000) | (e->phys_even + (i - e->start_even) + 0xFFF);
                if (e->d_even) tlb->LUT_w[i>>12] = tlb->LUT_r[i>>12];
                if (tlb->fastmem) r4300_fastmem_tlb_page(tlb, (uint32_t)(i >> 12));
            }
        }
    }

    if (e->v_odd)
    {
        if (e->start_odd < e->end_odd &&
            !(e->start_odd >= 0x80000000 && e->end_odd < 0xC0000000) &&
            e->phys_odd < 0x20000000)
        {
            for (i=e->start_odd;i<e->end_odd;i+=0x1000)
            {
                tlb->LUT_r[i>>12] = UINT32_C(0x80000000) | (e->phys_odd + (i - e->start_odd) + 0xFFF);
                if (e->d_odd) tlb->LUT_w[i>>12] = tlb->LUT_r[i>>12];
                if (tlb->fastmem) r4300_fastmem_tlb_page(tlb, (uint32_t)(i >> 12));
            }
        }
    }
}

uint32_t virtual_to_physical_address(struct r4300_core* r4300, uint32_t address, int w)
{
    const struct tlb* tlb = &r4300->cp0.tlb;
    unsigned int addr = address >> 12;

#ifdef JIT
    if (r4300->emumode == EMUMODE_DYNAREC)
    {
        intptr_t map = r4300->jit_hot_state.memory_map[addr];
        if ((tlb->LUT_w[addr]) && (w == 1))
        {
            assert(map == (((uintptr_t)r4300->rdram->dram + (uintptr_t)((tlb->LUT_w[addr] & 0xFFFFF000) - 0x80000000) - (address & 0xFFFFF000)) >> 2));
        }
        else if ((tlb->LUT_r[addr]) && (w == 0))
        {
            assert((map&~WRITE_PROTECT) == (((uintptr_t)r4300->rdram->dram + (uintptr_t)((tlb->LUT_r[addr] & 0xFFFFF000) - 0x80000000) - (address & 0xFFFFF000)) >> 2));
            if (map & WRITE_PROTECT)
            {
                assert(tlb->LUT_w[addr] == 0);
            }
        }
        else {
            assert(map < 0);
        }
    }
#endif

    {
        const uint32_t mapped = (w == 1) ? tlb->LUT_w[addr] : tlb->LUT_r[addr];
#if defined(__GNUC__) || defined(__clang__)
        if (__builtin_expect(mapped != 0, 1))
#else
        if (mapped != 0)
#endif
            return (mapped & UINT32_C(0xFFFFF000)) | (address & UINT32_C(0xFFF));
    }
    //printf("tlb exception !!! @ %x, %x, add:%x\n", address, w, r4300->pc->addr);
    //getchar();

#ifdef JIT
    if(IgnoreTLBExceptions == 0) {
        /* False, Default Behaviour */
        TLB_refill_exception(r4300, address, w);
    } else if(IgnoreTLBExceptions == 1) {
        /* OnlyNotEnabled */
        if(r4300->emumode == EMUMODE_DYNAREC)
        {
            if(using_tlb)
            {
                TLB_refill_exception(r4300, address, w);
            }
        } else {
            TLB_refill_exception(r4300, address, w);
        }
    } else if(IgnoreTLBExceptions == 2)
    {
        /* AlwaysIgnoreTLB */
        /* Use-case GdbStub... */
    }
#else
    if(IgnoreTLBExceptions == 2)
    {
        /* AlwaysIgnoreTLB */
        /* Use-case for Interpreter-only GdbStub... */
    } else {
        TLB_refill_exception(r4300, address, w);
    }
#endif // JIT

    //return 0x80000000;
    return 0x00000000;
}
