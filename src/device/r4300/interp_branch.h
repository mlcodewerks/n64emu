

#ifndef N64EMU_R4300_INTERP_BRANCH_H
#define N64EMU_R4300_INTERP_BRANCH_H
#include "r4300_core.h"
#include "cp0.h"
#include "timing.h"

static osal_force_inline int interp_direct_nop_delay(struct r4300_core* r)
{
    uint32_t pc = r->instruction_pc.addr;
    uint32_t status = r->cp0.regs[CP0_STATUS_REG];
    if (r->cache_timing || (pc & UINT32_C(0xc0000000)) != UINT32_C(0x80000000) ||
        ((status & CP0_STATUS_MODE_MASK) && !(status & (CP0_STATUS_EXL | CP0_STATUS_ERL))))
        return 0;
    if (*(uint32_t*)((uint8_t*)r->mem->base + (pc & UINT32_C(0x1ffffffc))) != 0)
        return 0;
    /* The delay-slot interrupt remains deferred until the branch resolves. */
    cp0_step_cycles(&r->cp0, 1 + r4300_issue_reservation(r->compat_issue_clock));
    ++r->retired_ops;
    r->instruction_pc.addr = pc + 4;
    r->regs[0] = 0;
    return 1;
}

static osal_force_inline void interp_branch_finish_step(struct r4300_core* r,
    int taken, uint32_t target, int likely, void (*execute_one)(struct r4300_core*), int direct_nop)
{
    unsigned saved_delay=r->delay_slot;
    uint32_t saved_target=r->branch_target,pc=r->instruction_pc.addr;
    if(saved_delay) {
        const uint32_t *word=fast_mem_access(r,pc);
        unsigned major=word?*word>>26:0;
        if(major==1||(major>=4&&major<=7)||(major>=20&&major<=23)||major==17)
            target+=saved_target-(pc+4);
    }
    uint32_t slot_pc=saved_delay?saved_target:pc+4;
    uint32_t sequence=++r->branch_sequence;
    r->branch_target=taken?target:slot_pc+4;
    if (!likely || taken) {
        if(taken&&r->cache_timing&&!r->compat_issue_clock&&
           ((target^r->instruction_pc.addr)&~UINT32_C(0xfff))==0&&r4300_access_cached(r,target))
            r4300_prefetch_access_cycles(r,target);
        r->instruction_pc.addr = slot_pc;
        r->delay_slot = 1;
        if (!direct_nop || !interp_direct_nop_delay(r)) execute_one(r);
        cp0_update_count(r);
        r->delay_slot = saved_delay;
        unsigned jump = (unsigned)taken & (unsigned)(r->skip_jump == 0)&(r->branch_sequence==sequence);
#if defined(N64EMU_INTERP_BRANCHLESS) && N64EMU_INTERP_BRANCHLESS
        uint32_t mask = 0u - jump;
        r->instruction_pc.addr = (target & mask) | (r->instruction_pc.addr & ~mask);
#else
        if (jump) r->instruction_pc.addr = target;
#endif
    } else {
        /* An annulled likely slot still occupies one pipeline clock; its
         * instruction and exceptions are suppressed. Fast's reserved issue
         * clock already supplies this bubble. */
        if (!r->compat_issue_clock) cp0_step_cycles(&r->cp0, 1);
        r->instruction_pc.addr = slot_pc+4;
        cp0_update_count(r);
    }
    r->branch_target=saved_target;
    if(!saved_delay)r->branch_sequence=0;
    r->cp0.last_addr = r->instruction_pc.addr;
    if (!r->delay_slot&&r->cp0.cycle_count >= 0) gen_interrupt(r);
}

static osal_force_inline void interp_branch_finish(struct r4300_core* r,
    int taken, uint32_t target, int likely)
{
    /* With guest cache timing disabled, an unmapped kernel NOP slot has
     * no effect beyond its issue clock and PC. Share the guarded shortcut
     * used by cached equality branches with all precise branch handlers. */
    interp_branch_finish_step(r, taken, target, likely, r->execute_one, 1);
}

static osal_force_inline void interp_idle_advance(struct r4300_core* r)
{
    cp0_update_count(r);
    if (r->cp0.cycle_count < 0) {
        uint64_t clocks=(uint64_t)(-r->cp0.cycle_count)*2-r->cp0.count_phase;
        r->retired_ops+=clocks/(1+r4300_issue_reservation(r->compat_issue_clock));
        r->cp0.regs[CP0_COUNT_REG] += (uint32_t)(-r->cp0.cycle_count);
        r->cp0.count_clock += -r->cp0.cycle_count;
        r->cp0.count_phase = 0;
        r->cp0.cycle_count = 0;
    }
}

static osal_force_inline int interp_equal_branch(struct r4300_core* r,
    uint32_t op, int idle, void (*execute_one)(struct r4300_core*))
{
    unsigned major = op >> 26;
    if (major != 4 && major != 5 && major != 20 && major != 21) return 0;
    int taken = r->regs[(op >> 21) & 31] == r->regs[(op >> 16) & 31];
    if (major & 1) taken = !taken;
    uint32_t target = r->instruction_pc.addr + ((int32_t)(int16_t)op + 1) * 4;
    if (taken && idle) interp_idle_advance(r);
    interp_branch_finish_step(r, taken, target, major >= 20, execute_one, 1);
    return 1;
}
#endif
