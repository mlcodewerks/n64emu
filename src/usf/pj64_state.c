/* Project64 USF state import. SPDX-License-Identifier: GPL-2.0-or-later */
#include "device/device.h"
#include "device/r4300/cpu_ops.h"
#include "device/r4300/interrupt.h"
#include "device/r4300/jit_runtime.h"
#include "device/rcp/rcp.h"
#include "device/rcp/rsp/p_rsp/dpc_bridge.h"
#include "internal.h"
#include "main/main.h"
#include <string.h>

static uint32_t word(const uint8_t **p) {
    const uint8_t *b = *p;
    *p += 4;
    return (uint32_t)b[0] | ((uint32_t)b[1] << 8) | ((uint32_t)b[2] << 16) | ((uint32_t)b[3] << 24);
}
static uint64_t dword(const uint8_t **p) {
    uint64_t lo = word(p);
    return lo | ((uint64_t)word(p) << 32);
}
static void words(uint32_t *out, const uint8_t **p, size_t n) {
    while (n--)
        *out++ = word(p);
}
int parallax_usf_load_pj64(const uint8_t *data, size_t size) {
    if (!data || size < 8 || memcmp(data, "\xc8\xa6\xd8\x23", 4))
        return 0;
    const uint8_t *p = data + 4;
    uint32_t ram_size = word(&p);
    if ((ram_size != 0x400000 && ram_size != 0x800000) || size < ram_size + 0x275cu)
        return 0;
    struct r4300_core *r = &g_dev.r4300;
    p += 64;
    uint32_t vi_timer = word(&p), pc = word(&p);
    for (unsigned i = 0; i < 32; ++i)
        r->regs[i] = (int64_t)dword(&p);
    for (unsigned i = 0; i < 32; ++i)
        r->cp1.regs[i].dword = (int64_t)dword(&p);
    words(r->cp0.regs, &p, 32);
    cp0_reset_extended(&r->cp0);
    r->cp1.fcr0 = word(&p);
    p += 30 * 4;
    r->cp1.fcr31 = word(&p);
    set_fpr_pointers(&r->cp1, r->cp0.regs[CP0_STATUS_REG]);
    update_x86_rounding_mode(&r->cp1);
    r->hi = (int64_t)dword(&p);
    r->lo = (int64_t)dword(&p);
    words(g_dev.rdram.regs[0], &p, 10);
    words(g_dev.sp.regs, &p, 8);
    words(g_dev.sp.regs2, &p, 2);
    words(g_dev.dp.dpc_regs, &p, 8);
    p += 8;
    words(g_dev.mi.regs, &p, 4);
    words(g_dev.vi.regs, &p, 14);
    words(g_dev.ai.regs, &p, 6);
    g_dev.ai.regs[AI_DACRATE_REG] &= 0x3fff;
    words(g_dev.pi.regs, &p, 13);
    words(g_dev.ri.regs, &p, 8);
    words(g_dev.si.regs, &p, 4);
    poweron_tlb(&r->cp0.tlb);
    for (unsigned i = 0; i < 32; ++i) {
        (void)word(&p);
        uint32_t mask = word(&p), hi = word(&p), lo0 = word(&p), lo1 = word(&p);
        struct tlb_entry *t = &r->cp0.tlb.entries[i];
        t->g = (lo0 & lo1) & 1;
        t->pfn_even = (lo0 >> 6) & 0xfffff;
        t->pfn_odd = (lo1 >> 6) & 0xfffff;
        t->c_even = (lo0 >> 3) & 7;
        t->c_odd = (lo1 >> 3) & 7;
        t->d_even = (lo0 >> 2) & 1;
        t->d_odd = (lo1 >> 2) & 1;
        t->v_even = (lo0 >> 1) & 1;
        t->v_odd = (lo1 >> 1) & 1;
        t->asid = hi & 255;
        t->vpn2 = (hi & 0xffffe000) >> 13;
        t->mask = (mask & 0x1ffe000) >> 13;
        t->start_even = t->vpn2 << 13;
        t->end_even = t->start_even + (t->mask << 12) + 0xfff;
        t->phys_even = t->pfn_even << 12;
        t->start_odd = t->end_even + 1;
        t->end_odd = t->start_odd + (t->mask << 12) + 0xfff;
        t->phys_odd = t->pfn_odd << 12;
        tlb_map(&r->cp0.tlb, i);
    }
    memcpy(g_dev.pif.ram, p, 64);
    p += 64;
    memset(g_dev.rdram.dram, 0, RDRAM_MAX_SIZE);
    words(g_dev.rdram.dram, &p, ram_size / 4);
    words(g_dev.sp.mem, &p, 8192 / 4);
    if ((size_t)(p - data) != ram_size + 0x275cu)
        return 0;
    r->pc = &r->instruction_pc;
    r->instruction_pc.addr = r->cp0.last_addr = pc;
    r->startup = 0;
    r->stop = 0;
    r->regs[0] = 0;
    if (!r4300_jit_init(r))
        return 0;
#ifdef HAVE_PARALLAXJIT
    r->execute_one = r4300_jit_execute_one;
#else
    r->execute_one = r4300_ops_execute_one;
#endif
    r4300_jit_invalidate(r, 0, 0);
    uint32_t events[] = {VI_INT, r->cp0.regs[CP0_COUNT_REG] + vi_timer, 0xffffffff};
    load_eventqueue_infos(&r->cp0, (const char *)events);
    vi_rebase_timing(&g_dev.vi, vi_timer ? vi_timer : 781250);
    g_dev.ai.regs[AI_STATUS_REG] = 0;
    memset(g_dev.ai.fifo, 0, sizeof(g_dev.ai.fifo));
    g_dev.ai.last_read = 0;
    g_dev.ai.samples_format_changed = 1;
    ai_rebase_timing(&g_dev.ai);
    if (parallax_usf_fifo_enabled()) {
        g_dev.ai.regs[AI_STATUS_REG] = 0x40000000;
        add_interrupt_event(&r->cp0, AI_INT, 1);
    }
    uint32_t rsp_state[PARALLEL_RSP_STATE_WORDS] = {0};
    rsp_state[0] = g_dev.sp.regs2[SP_PC_REG] & 0xffc;
    parallelRSPLoadState(rsp_state);
    return 1;
}
