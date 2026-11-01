

#ifndef N64EMU_R4300_INTERP_MEMORY_H
#define N64EMU_R4300_INTERP_MEMORY_H
#include "r4300_core.h"
#include "timing.h"
#include "tlb.h"
#include "device/memory.h"
#include "device/rdram.h"
#include "core/log.h"

#if defined(__GNUC__) || defined(__clang__)
#define INTERP_MEM_LIKELY(x) __builtin_expect(!!(x), 1)
#define INTERP_MEM_UNLIKELY(x) __builtin_expect(!!(x), 0)
#else
#define INTERP_MEM_LIKELY(x) (x)
#define INTERP_MEM_UNLIKELY(x) (x)
#endif

/* Interpreter-side TLB hit path. The existing translator remains the sole
 * miss/exception path, so refill behavior and JIT validation stay
 * unchanged. w == 1 selects the writable LUT; all other values use read LUT. */
static osal_force_inline uint32_t interp_translate_address(struct r4300_core* r4300,
                                                           uint32_t address,
                                                           int w)
{
#if defined(JIT)
    if (INTERP_MEM_UNLIKELY(r4300->emumode == EMUMODE_DYNAREC))
        return virtual_to_physical_address(r4300, address, w);

#endif

    const uint32_t page = address >> 12;
    const uint32_t mapped = (w == 1)
        ? r4300->cp0.tlb.LUT_w[page]
        : r4300->cp0.tlb.LUT_r[page];

    if (INTERP_MEM_LIKELY(mapped != 0))
        return (mapped & UINT32_C(0xfffff000)) | (address & UINT32_C(0x00000fff));

    return virtual_to_physical_address(r4300, address, w);
}

/* Keep the dynamic memory map authoritative. Normal RDRAM is overwhelmingly
 * common for Conker, so inline its tiny handler after confirming the currently
 * installed handler is the normal one. Debug/breakpoint/corruption handlers
 * still take the generic dispatch path. */
static osal_force_inline void interp_mem_read32(const struct mem_handler* handler,
                                                uint32_t address,
                                                uint32_t* value)
{
    if (INTERP_MEM_LIKELY(handler->read32 == read_rdram_dram))
    {
        const struct rdram* rdram = (const struct rdram*)handler->opaque;
        *value = rdram->dram[rdram_dram_address(address)];
        return;
    }

    handler->read32(handler->opaque, address, value);
}

static osal_force_inline void interp_mem_write32(const struct mem_handler* handler,
                                                 uint32_t address,
                                                 uint32_t value,
                                                 uint32_t mask)
{
    if (INTERP_MEM_LIKELY(handler->write32 == write_rdram_dram))
    {
        struct rdram* rdram = (struct rdram*)handler->opaque;
        masked_write(&rdram->dram[rdram_dram_address(address)], value, mask);
        return;
    }

    handler->write32(handler->opaque, address, value, mask);
}

/* Recent libdragon uses uncached XKPHYS for PI/MMIO. Preserve the full
 * effective address until this decode, including 32-bit physical addresses
 * outside the compatibility KSEG windows. Other extended segments retain
 * the existing translator; this is not an extended-TLB implementation. */
static osal_force_inline int interp_xkphys_uncached(struct r4300_core *r,
    uint64_t effective, uint32_t *physical)
{
    if ((effective >> 32) != UINT32_C(0x90000000)) return 0;
    uint32_t status = r->cp0.regs[CP0_STATUS_REG];
    if (!(status & CP0_STATUS_KX) ||
        (!(status & (CP0_STATUS_EXL | CP0_STATUS_ERL)) && (status & 0x18))) return 0;
    *physical = (uint32_t)effective;
    return 1;
}

static osal_force_inline void interp_xkphys_store_invalidate(struct r4300_core *r,
    uint32_t physical, unsigned bytes)
{
    if (physical < UINT32_C(0x20000000) && r->cached_code_pages) {
        uint32_t page = physical >> 12;
        if ((r->cached_code_pages[page >> 6] >> (page & 63)) & 1)
            invalidate_r4300_cached_code(r, physical, bytes);
    }
}

/* MI repeats an uncached doubleword write across up to 128 bytes. The row
 * counter wraps within 2 KiB. libdragon's hardware memset uses full SDs;
 * partial bus writes continue through the ordinary masked-store path. */
static osal_force_inline int interp_mi_repeat_dword(struct r4300_core *r,
    uint32_t physical, uint64_t value, uint64_t mask)
{
    if (mask != UINT64_MAX || physical >= UINT32_C(0x03f00000) || !r->mi ||
        !(r->mi->regs[MI_INIT_MODE_REG] & 0x80)) return 0;
    unsigned length = (r->mi->regs[MI_INIT_MODE_REG] & 0x7f) + 1;
    r->mi->regs[MI_INIT_MODE_REG] &= ~UINT32_C(0x80);
    for (unsigned i=0; i<length; i++) {
        uint32_t address = (physical & ~UINT32_C(0x7ff)) | ((physical + i) & 0x7ff);
        unsigned shift = 24 - (address & 3) * 8;
        uint32_t byte = (uint32_t)(value >> (56 - (i & 7) * 8)) & 0xff;
        interp_xkphys_store_invalidate(r, address, 1);
        interp_mem_write32(&r->mem->handlers[address >> 16], address & ~UINT32_C(3),
            byte << shift, UINT32_C(0xff) << shift);
    }
    return 1;
}

static osal_force_inline int interp_read_aligned_word(struct r4300_core* r4300,
                                                      uint64_t effective,
                                                      uint32_t* value)
{
    uint32_t address = (uint32_t)effective;
    if (INTERP_MEM_UNLIKELY(interp_xkphys_uncached(r4300, effective, &address))) {
        r4300_data_access_cycles(r4300, UINT32_C(0xa0000000), address, 0);
        address &= ~UINT32_C(3);
        interp_mem_read32(&r4300->mem->handlers[address >> 16], address, value);
        return 1;
    }
    const uint32_t virtual_address = address;
    if (INTERP_MEM_UNLIKELY((address & UINT32_C(0xc0000000)) != UINT32_C(0x80000000)))
    {
        address = interp_translate_address(r4300, address, 0);
        if (INTERP_MEM_UNLIKELY(address == 0))
            return 0;
    }

    if (r4300_data_access_cycles(r4300, virtual_address, address, 0)) {
        *value = r4300->dcache_words[(virtual_address >> 4) & 511][(address >> 2) & 3];
        return 1;
    }
    address &= UINT32_C(0x1ffffffc);
    interp_mem_read32(&r4300->mem->handlers[address >> 16], address, value);
    return 1;
}

static osal_force_inline int interp_read_aligned_dword(struct r4300_core* r4300,
                                                       uint64_t effective,
                                                       uint64_t* value)
{
    uint32_t address = (uint32_t)effective;
    if (INTERP_MEM_UNLIKELY(interp_xkphys_uncached(r4300, effective, &address))) {
        uint32_t hi, lo;
        r4300_data_access_cycles(r4300, UINT32_C(0xa0000000), address, 0);
        address &= ~UINT32_C(3);
        const struct mem_handler *handler = &r4300->mem->handlers[address >> 16];
        interp_mem_read32(handler, address, &hi);
        interp_mem_read32(handler, address + 4, &lo);
        *value = ((uint64_t)hi << 32) | lo;
        return 1;
    }
    const uint32_t virtual_address = address;
    uint32_t w0;
    uint32_t w1;

    if (INTERP_MEM_UNLIKELY((address & UINT32_C(7)) != 0))
        core_log(CORE_LOG_WARNING, "Unaligned dword read %08x", address);

    if (INTERP_MEM_UNLIKELY((address & UINT32_C(0xc0000000)) != UINT32_C(0x80000000)))
    {
        address = interp_translate_address(r4300, address, 0);
        if (INTERP_MEM_UNLIKELY(address == 0))
            return 0;
    }

    if (r4300_data_access_cycles(r4300, virtual_address, address, 0)) {
        uint32_t* words = r4300->dcache_words[(virtual_address >> 4) & 511];
        *value = ((uint64_t)words[(address >> 2) & 3] << 32) | words[((address >> 2) & 3) + 1];
        return 1;
    }
    address &= UINT32_C(0x1ffffffc);
    const struct mem_handler* handler = &r4300->mem->handlers[address >> 16];
    interp_mem_read32(handler, address + 0, &w0);
    interp_mem_read32(handler, address + 4, &w1);
    *value = ((uint64_t)w0 << 32) | w1;
    return 1;
}

static osal_force_inline int interp_write_aligned_word(struct r4300_core* r4300,
                                                       uint64_t effective,
                                                       uint32_t value,
                                                       uint32_t mask)
{
    uint32_t address = (uint32_t)effective;
    if (INTERP_MEM_UNLIKELY(interp_xkphys_uncached(r4300, effective, &address))) {
        interp_xkphys_store_invalidate(r4300, address, 4);
        r4300_data_access_cycles(r4300, UINT32_C(0xa0000000), address, 1);
        address &= ~UINT32_C(3);
        interp_mem_write32(&r4300->mem->handlers[address >> 16], address, value, mask);
        return 1;
    }
    const uint32_t virtual_address = address;
    if (INTERP_MEM_UNLIKELY((address & UINT32_C(0xc0000000)) != UINT32_C(0x80000000)))
    {
        address = interp_translate_address(r4300, address, 1);
        if (INTERP_MEM_UNLIKELY(address == 0))
            return 0;
    }

    /* Translation is complete: a clear physical-page bit means no decoded
     * instruction can be invalidated by this store. Page generations still
     * protect all old blocks after the first write clears that bit. */
    uint32_t code_page = (address & UINT32_C(0x1fffffff)) >> 12;
    if (INTERP_MEM_UNLIKELY(r4300->cached_code_pages != NULL &&
        ((r4300->cached_code_pages[code_page >> 6] >> (code_page & 63)) & 1)))
        invalidate_r4300_cached_code(r4300, address, 4);

    if (r4300_data_access_cycles(r4300, virtual_address, address, 1)) {
        masked_write(&r4300->dcache_words[(virtual_address >> 4) & 511][(address >> 2) & 3], value, mask);
        return 1;
    }
    address &= UINT32_C(0x1ffffffc);
    interp_mem_write32(&r4300->mem->handlers[address >> 16], address, value, mask);
    return 1;
}

static osal_force_inline int interp_write_aligned_dword(struct r4300_core* r4300,
                                                        uint64_t effective,
                                                        uint64_t value,
                                                        uint64_t mask)
{
    uint32_t address = (uint32_t)effective;
    if (INTERP_MEM_UNLIKELY(interp_xkphys_uncached(r4300, effective, &address))) {
        interp_xkphys_store_invalidate(r4300, address, 8);
        r4300_data_access_cycles(r4300, UINT32_C(0xa0000000), address, 1);
        address &= ~UINT32_C(3);
        if (interp_mi_repeat_dword(r4300, address, value, mask)) return 1;
        const struct mem_handler *handler = &r4300->mem->handlers[address >> 16];
        interp_mem_write32(handler, address, (uint32_t)(value >> 32), (uint32_t)(mask >> 32));
        if ((address & UINT32_C(0xffffe000)) != UINT32_C(0x04000000))
            interp_mem_write32(handler, address + 4, (uint32_t)value, (uint32_t)mask);
        return 1;
    }
    const uint32_t virtual_address = address;
    if (INTERP_MEM_UNLIKELY((address & UINT32_C(7)) != 0))
        core_log(CORE_LOG_WARNING, "Unaligned dword write %08x", address);

    if (INTERP_MEM_UNLIKELY((address & UINT32_C(0xc0000000)) != UINT32_C(0x80000000)))
    {
        address = interp_translate_address(r4300, address, 1);
        if (INTERP_MEM_UNLIKELY(address == 0))
            return 0;
    }

    uint32_t code_page = (address & UINT32_C(0x1fffffff)) >> 12;
    if (INTERP_MEM_UNLIKELY(r4300->cached_code_pages != NULL &&
        ((r4300->cached_code_pages[code_page >> 6] >> (code_page & 63)) & 1)))
        invalidate_r4300_cached_code(r4300, address, 8);

    if (r4300_data_access_cycles(r4300, virtual_address, address, 1)) {
        uint32_t* words = r4300->dcache_words[(virtual_address >> 4) & 511];
        masked_write(&words[(address >> 2) & 3], (uint32_t)(value >> 32), (uint32_t)(mask >> 32));
        masked_write(&words[((address >> 2) & 3) + 1], (uint32_t)value, (uint32_t)mask);
        return 1;
    }
    address &= UINT32_C(0x1ffffffc);
    const struct mem_handler* handler = &r4300->mem->handlers[address >> 16];
    if (!r4300_access_cached(r4300, virtual_address) &&
        interp_mi_repeat_dword(r4300, address, value, mask)) return 1;
    interp_mem_write32(handler, address + 0, (uint32_t)(value >> 32), (uint32_t)(mask >> 32));
    /* SP memory accepts only the first word of a CPU doubleword store. */
    if((address&UINT32_C(0xffffe000))==UINT32_C(0x04000000))return 1;
    interp_mem_write32(handler, address + 4, (uint32_t)value, (uint32_t)mask);
    return 1;
}

static osal_force_inline int check_alignment(struct r4300_core* r4300, uint64_t address, unsigned int mask, int store)
{
    if ((address & mask) == 0) return 0;
    cp0_set_fault_address(r4300, (address >> 32) == UINT32_C(0x90000000) ?
        address : (uint64_t)(int64_t)(int32_t)address);
    r4300->cp0.regs[CP0_CAUSE_REG] =
        (r4300->cp0.regs[CP0_CAUSE_REG] & ~UINT32_C(0x3000007c)) |
        (store ? CP0_CAUSE_EXCCODE_ADES : CP0_CAUSE_EXCCODE_ADEL);
    exception_general(r4300);
    return 1;
}

/* Shared constructors keep both dispatchers on the same exception/access path. */
static osal_force_inline void interp_load_word(struct r4300_core* r, uint32_t op, int sign)
{
    unsigned rt = (op >> 16) & 31;
    uint64_t address = (uint64_t)r->regs[(op >> 21) & 31] + (uint64_t)(int64_t)(int16_t)op;
    if (check_alignment(r, address, 3, 0)) return;
    r->instruction_pc.addr += 4;
    uint32_t value;
    if (interp_read_aligned_word(r, address, &value))
        r->regs[rt] = sign ? (int64_t)(int32_t)value : (int64_t)value;
}

static osal_force_inline void interp_store_word(struct r4300_core* r, uint32_t op)
{
    unsigned rt = (op >> 16) & 31;
    uint64_t address = (uint64_t)r->regs[(op >> 21) & 31] + (uint64_t)(int64_t)(int16_t)op;
    if (check_alignment(r, address, 3, 1)) return;
    r->instruction_pc.addr += 4;
    interp_write_aligned_word(r, address, (uint32_t)r->regs[rt], ~UINT32_C(0));
}

static osal_force_inline int interp_word_memory(struct r4300_core* r, uint32_t op)
{
    unsigned major = op >> 26;
    if (major == 35 || major == 39) interp_load_word(r, op, major == 35);
    else if (major == 43) interp_store_word(r, op);
    else return 0;
    return 1;
}

#undef INTERP_MEM_LIKELY
#undef INTERP_MEM_UNLIKELY

#endif
