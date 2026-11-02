

#include "r4300_core.h"
#ifdef __LIBRETRO__
#include "renderer_options.h"
#endif
#include "jit_runtime.h"
#if defined(COUNT_INSTR)
#include "instr_counters.h"
#endif
#include "cpu_ops.h"
#include "timing.h"

#include "core/log.h"
#include "core/types.h"
#include "main/main.h"

#include <stdlib.h>
#include <string.h>
#include <time.h>

void init_r4300(struct r4300_core* r4300, struct memory* mem, struct mi_controller* mi, struct rdram* rdram, const struct interrupt_handler* interrupt_handlers,
    unsigned int emumode, int no_compiled_jump, int randomize_interrupt, uint32_t start_address)
{

    (void)emumode;
#ifdef HAVE_N64EMUJIT
    r4300->emumode = EMUMODE_DYNAREC;
#else
    r4300->emumode = EMUMODE_INTERPRETER;
#endif
    init_cp0(&r4300->cp0, interrupt_handlers);
    init_cp1(&r4300->cp1);

    r4300->mem = mem;
    r4300->mi = mi;
    r4300->vi = NULL;
    r4300->rdram = rdram;
    r4300->randomize_interrupt = randomize_interrupt;
    r4300->start_address = start_address;
    r4300->execute_one = NULL;
    r4300->jit = NULL;
    r4300->cached_code_pages = NULL;
    srand((unsigned int) time(NULL));
}

void poweron_r4300(struct r4300_core* r4300)
{
    /* clear registers */
    memset(r4300_regs(r4300), 0, 32*sizeof(int64_t));
    *r4300_mult_hi(r4300) = 0;
    *r4300_mult_lo(r4300) = 0;
    r4300->llbit = 0;
    r4300->cp2_latch = 0;

    *r4300_pc_struct(r4300) = NULL;
    r4300->delay_slot = 0;
    r4300->skip_jump = 0;
    r4300->pc_extension = 0;
    r4300->reset_hard_job = 0;
    r4300->stop = 0;
    r4300->startup =1;
    r4300_pipeline_reset(r4300);
    r4300_itlb_reset(r4300);
    memset(r4300->icache_tags, 0, sizeof(r4300->icache_tags));
    memset(r4300->icache_shadow_tags, 0, sizeof(r4300->icache_shadow_tags));
    memset(r4300->icache_shadow_words, 0, sizeof(r4300->icache_shadow_words));
    memset(r4300->dcache_tags, 0, sizeof(r4300->dcache_tags));
    memset(r4300->dcache_words, 0, sizeof(r4300->dcache_words));
    memset(r4300->write_buffer_end,0,sizeof(r4300->write_buffer_end));
    r4300->write_buffer_count=0;
    r4300->count_write_valid=0;r4300->count_write_clock=0;r4300->count_write_value=0;
    r4300->prefetch_pc=r4300->prefetch_op=r4300->prefetch_valid=0;
    r4300->retired_ops=r4300->random_epoch=0;r4300->random_value=31;r4300->random_bound=0;r4300->random_wired_count=0;
    r4300->branch_target=r4300->branch_sequence=0;
    r4300->irq_writeback_deadline=0;r4300->irq_writeback_pending=0;
    r4300->icache_fill_end=0;r4300->icache_fill_index=0;
    memset(r4300->random_wired_deadline,0,sizeof(r4300->random_wired_deadline));memset(r4300->random_wired_value,0,sizeof(r4300->random_wired_value));
    r4300->cache_timing = 1;
    r4300->compat_issue_clock = 1;
#ifdef __LIBRETRO__
    r4300->cache_timing = renderer_settings.per_cycle_timing ? 1 :
        renderer_settings.fast_cpu_timing ? 2 : 0;
    /* Fast and Accurate share native issue/interlock timing. They differ in
     * cache storage; only cache timing Off keeps the compatibility envelope. */
    r4300->compat_issue_clock = r4300->cache_timing == 0;
#endif

    r4300_jit_invalidate(r4300, 0, 0);

    /* setup CP0 registers */
    poweron_cp0(&r4300->cp0);

    /* setup CP1 registers */
    poweron_cp1(&r4300->cp1);
}

int64_t* r4300_regs(struct r4300_core* r4300)
{

    return r4300->regs;
}

int64_t* r4300_mult_hi(struct r4300_core* r4300)
{
    return &r4300->hi;
}

int64_t* r4300_mult_lo(struct r4300_core* r4300)
{
    return &r4300->lo;
}

unsigned int* r4300_llbit(struct r4300_core* r4300)
{
    return &r4300->llbit;
}

uint32_t* r4300_pc(struct r4300_core* r4300)
{
    return &(*r4300_pc_struct(r4300))->addr;
}

struct precomp_instr** r4300_pc_struct(struct r4300_core* r4300)
{
    return &r4300->pc;
}

int* r4300_stop(struct r4300_core* r4300)
{
    return &r4300->stop;
}

unsigned int get_r4300_emumode(struct r4300_core* r4300)
{
    return r4300->emumode;
}

uint32_t *fast_mem_access(struct r4300_core* r4300, uint32_t address)
{
    /* This code is performance critical for precise instruction helpers.
     * Removing error checking saves some time, but the emulator may crash. */

    uint32_t virtual_address = address;
    int cached;
    if (r4300->pc_extension) {
        uint64_t effective = (r4300->pc_extension & UINT64_C(0xffffffff00000000)) | address;
        if (!r4300_translate_address64(r4300, effective, 2, &address, &cached)) return NULL;
    } else if ((address & UINT32_C(0xc0000000)) != UINT32_C(0x80000000)) {
        address = virtual_to_physical_address(r4300, address, 2);
        if (address == 0) // TLB exception
            return NULL;
    }

    address &= UINT32_C(0x1ffffffc);
    uint32_t status = r4300->cp0.regs[CP0_STATUS_REG];
    if ((status & (CP0_STATUS_RE | CP0_STATUS_MODE_MASK | CP0_STATUS_EXL | CP0_STATUS_ERL)) ==
        (CP0_STATUS_RE | UINT32_C(0x10))) address ^= 4;

    if (r4300->cache_timing && (r4300->pc_extension ? cached : r4300_access_cached(r4300,virtual_address))) {
        unsigned index=(virtual_address>>5)&511;
        uint32_t key=(address&0x1ffff000u)|1;
        if(r4300->icache_tags[index]==key && r4300->icache_shadow_tags[index]==key)
            return &r4300->icache_shadow_words[index][(address>>2)&7];
    }

    return mem_base_u32(r4300->mem->base, address);
}

/* Read aligned word from memory.
 * address may not be word-aligned for byte or hword accesses.
 * Alignment is taken care of when calling mem handler.
 */
int r4300_read_aligned_word(struct r4300_core* r4300, uint32_t address, uint32_t* value)
{
    if ((address & UINT32_C(0xc0000000)) != UINT32_C(0x80000000)) {
        address = virtual_to_physical_address(r4300, address, 0);
        if (address == 0) {
            return 0;
        }
    }

    address &= UINT32_C(0x1ffffffc);

    mem_read32(mem_get_handler(r4300->mem, address), address & ~UINT32_C(3), value);

    return 1;
}

/* Read aligned dword from memory */
int r4300_read_aligned_dword(struct r4300_core* r4300, uint32_t address, uint64_t* value)
{
    uint32_t w[2];

    /* XXX: unaligned dword accesses should trigger a address error,
     * but inaccurate timing of the core can lead to unaligned address on reset
     * so just emit a warning and keep going */
    if ((address & 0x7) != 0) {
        core_log(CORE_LOG_WARNING, "Unaligned dword read %08x", address);
    }

    if ((address & UINT32_C(0xc0000000)) != UINT32_C(0x80000000)) {
        address = virtual_to_physical_address(r4300, address, 0);
        if (address == 0) {
            return 0;
        }
    }

    address &= UINT32_C(0x1ffffffc);

    const struct mem_handler* handler = mem_get_handler(r4300->mem, address);
    mem_read32(handler, address + 0, &w[0]);
    mem_read32(handler, address + 4, &w[1]);

    *value = ((uint64_t)w[0] << 32) | w[1];

    return 1;
}

/* Write aligned word to memory.
 * address may not be word-aligned for byte or hword accesses.
 * Alignment is taken care of when calling mem handler.
 */
int r4300_write_aligned_word(struct r4300_core* r4300, uint32_t address, uint32_t value, uint32_t mask)
{
    if ((address & UINT32_C(0xc0000000)) != UINT32_C(0x80000000)) {
        address = virtual_to_physical_address(r4300, address, 1);
        if (address == 0) {
            return 0;
        }
    }

    invalidate_r4300_cached_code(r4300, address, 4);
    address &= UINT32_C(0x1ffffffc);

    mem_write32(mem_get_handler(r4300->mem, address), address & ~UINT32_C(3), value, mask);

    return 1;
}

/* Write aligned dword to memory */
int r4300_write_aligned_dword(struct r4300_core* r4300, uint32_t address, uint64_t value, uint64_t mask)
{
    /* XXX: unaligned dword accesses should trigger a address error,
     * but inaccurate timing of the core can lead to unaligned address on reset
     * so just emit a warning and keep going */
    if ((address & 0x7) != 0) {
        core_log(CORE_LOG_WARNING, "Unaligned dword write %08x", address);
    }

    if ((address & UINT32_C(0xc0000000)) != UINT32_C(0x80000000)) {
        address = virtual_to_physical_address(r4300, address, 1);
        if (address == 0) {
            return 0;
        }
    }

    invalidate_r4300_cached_code(r4300, address, 8);
    address &= UINT32_C(0x1ffffffc);

    const struct mem_handler* handler = mem_get_handler(r4300->mem, address);
    mem_write32(handler, address + 0, value >> 32,      mask >> 32);
    mem_write32(handler, address + 4, (uint32_t) value, (uint32_t) mask      );

    return 1;
}

void invalidate_r4300_cached_code(struct r4300_core* r4300, uint32_t address, size_t size)
{
    r4300_jit_invalidate(r4300, address, size);
}


void generic_jump_to(struct r4300_core* r4300, uint32_t address)
{
    r4300->pc_extension = 0;
    *r4300_pc(r4300) = address;
}

void generic_jump_to64(struct r4300_core* r4300, uint64_t address)
{
    generic_jump_to(r4300, (uint32_t)address);
    if (address != (uint64_t)(int64_t)(int32_t)address)
        r4300->pc_extension = (address & UINT64_C(0xffffffff00000000)) | 1;
}


/* XXX: not really a good interface but it gets the job done... */
void savestates_load_set_pc(struct r4300_core* r4300, uint32_t pc)
{
    generic_jump_to(r4300, pc);
    invalidate_r4300_cached_code(r4300, 0, 0);
}
