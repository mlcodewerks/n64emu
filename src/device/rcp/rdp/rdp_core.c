

#include "rdp_core.h"
#include "device/rcp/rcp.h"

#include <string.h>

#include "device/memory.h"
#include "device/rdram.h"
#include "device/r4300/cp0.h"
#include "device/r4300/r4300_core.h"
#include "device/rcp/rcp.h"
#include "device/rcp/rsp/rsp_core.h"
#include "device/rcp/rsp/rsp_timing.h"
#if defined(HAVE_PARALLEL_RSP)
#include "device/rcp/rsp/p_rsp/dpc_bridge.h"
#endif
#include "core/backend.h"
#ifdef __LIBRETRO__
#include "renderer_options.h"
#include "softrdp/softrdp_video.h"
#endif

static void observe_busy(void *context, int64_t begin, int64_t end, unsigned mask)
{
    rdp_counters_work(&((struct rdp_core *)context)->counters, begin, end, mask);
}
static void attach_timing(struct rdp_core *dp)
{
    dp->timing.observe_busy = observe_busy;
    dp->timing.busy_context = dp;
    dp->timing.rdram_bytes = dp->fb.rdram ? dp->fb.rdram->dram_size : 0;
    const struct vi_controller *vi = dp->mi->r4300->vi;
    dp->timing.vi_regs = dp->vi_regs;
    dp->timing.vi_field_start = vi ? vi->field_start_count_clock : 0;
    dp->timing.vi_field_ticks = vi ? vi->field_ticks : 0;
#ifndef N64EMU_USF
    dp->timing.overlap_span_startup = !dp->mi->r4300->cache_timing;
#endif
    if (dp->timing.validate_uniform_ram) {
        /* Restoring clears frontend framebuffer protection. Verify every
         * saved proof against synchronized RDRAM before its first use, so
         * intervening untracked CPU stores cannot leave a stale proof. */
        for (unsigned n = 0; n < 8; ++n) {
            struct rdp_uniform_depth *u = &dp->timing.uniform[n];
            if (!u->valid) continue;
            uint64_t pixels = (uint64_t)u->width * u->height;
            if ((u->address & 7) || (uint64_t)u->address + pixels * 2 > dp->timing.rdram_bytes) {
                u->valid = 0; continue;
            }
            for (uint64_t i = 0; i < pixels; ++i) {
                uint32_t address = u->address + (uint32_t)(i * 2);
                uint16_t packed = (uint16_t)(dp->fb.rdram->dram[address >> 2] >> ((address & 2) ? 0 : 16));
                if ((packed & 3) || rdp_timing_decompress_z(packed) != u->z) { u->valid = 0; break; }
            }
        }
        dp->timing.validate_uniform_ram = 0;
    }
}

uint32_t rdp_dpc_clock_value(const struct rdp_core* dp, uint32_t count)
{
    uint32_t elapsed = count - dp->clock_base;
    uint32_t advance = (uint32_t)(((uint64_t)elapsed * 4u) / 3u);

    return (dp->clock_offset + advance) & UINT32_C(0x00ffffff);
}

void rdp_restore_dpc_clock(struct rdp_core* dp, uint32_t count)
{
    dp->clock_base = count;
    dp->clock_offset = dp->dpc_regs[DPC_CLOCK_REG] & UINT32_C(0x00ffffff);
}

static uint32_t dpc_clock_now(struct rdp_core* dp, int from_rsp)
{
    const uint32_t* cp0_regs;

    cp0_update_count(dp->mi->r4300);
    cp0_regs = r4300_cp0_regs(&dp->mi->r4300->cp0);
    uint32_t clock = rdp_dpc_clock_value(dp, cp0_regs[CP0_COUNT_REG]);
#if defined(HAVE_PARALLEL_RSP)
    /* The synchronous RSP slice has not reached CPU COUNT yet. */
    if (from_rsp) {
        uint32_t start = cp0_regs[CP0_COUNT_REG];
        if (dp->instruction_timing)
            start += (uint32_t)(dp->sp->slice_start_clock - dp->mi->r4300->cp0.count_clock);
        clock = rdp_dpc_clock_value(dp, start) + parallelRSPExecutedCycles();
    }
#else
    (void)from_rsp;
#endif
    return clock & UINT32_C(0x00ffffff);
}

static int64_t busy_now(struct rdp_core *dp, int from_rsp)
{
    cp0_update_count(dp->mi->r4300);
#if defined(HAVE_PARALLEL_RSP)
    if (from_rsp) return dp->sp->slice_start_clock * 4 +
        (int64_t)parallelRSPExecutedCycles() * 3 - dp->sp->slice_overhang;
#else
    (void)from_rsp;
#endif
    return dp->mi->r4300->cp0.count_clock * 4;
}
static void retire_counters(struct rdp_core *dp, int64_t now)
{
    int64_t horizon = dp->mi->r4300->cp0.count_clock * 4;
    if (dp->sp->rsp_task_locked && dp->sp->slice_start_clock * 4 < horizon)
        horizon = dp->sp->slice_start_clock * 4;
    if (now < horizon) horizon = now;
    rdp_counters_advance(&dp->counters, horizon);
}
static void read_busy(struct rdp_core *dp, unsigned reg, int from_rsp)
{
    if (reg == DPC_STATUS_REG && dp->pipe_sync_deadline > 0 &&
        busy_now(dp, from_rsp) >= dp->pipe_sync_deadline * 4) {
        dp->dpc_regs[DPC_STATUS_REG] &= ~UINT32_C(0x28);
        dp->pipe_sync_deadline = 0;
    }
    if (!dp->instruction_timing || reg < DPC_BUFBUSY_REG || reg > DPC_TMEM_REG) return;
    int64_t now = busy_now(dp, from_rsp);
    dp->dpc_regs[reg] = rdp_counter_value(&dp->counters, reg - DPC_BUFBUSY_REG, now);
    retire_counters(dp, now);
}

static int vi_uses_alternate_rows(const uint32_t* vi, uint32_t fb_width)
{
    return vi && (vi[VI_STATUS_REG] & 0x40u) && fb_width &&
        (vi[VI_WIDTH_REG] & 4095u) == fb_width * 2u;
}

uint32_t rdp_sync_full_delay(const struct rdp_core* dp)
{
    /* The fallback estimates a whole frame from programmed VI geometry. */
    /* An idle FullSync has one RDP delivery clock, rounded up to one COUNT
     * tick. Any known queued work is accounted for by the caller. */
    const uint32_t min_delay = 1;
    if (!dp->vi_regs) return min_delay;
    const uint32_t* vi = dp->vi_regs;
    uint32_t hs = (vi[VI_H_START_REG] >> 16) & 1023, he = vi[VI_H_START_REG] & 1023;
    uint32_t vs = (vi[VI_V_START_REG] >> 16) & 1023, ve = vi[VI_V_START_REG] & 1023;
    uint32_t pitch = vi[VI_WIDTH_REG] & 4095;
    uint32_t x_scale = vi[VI_X_SCALE_REG] & 4095, y_scale = vi[VI_Y_SCALE_REG] & 4095;
    if (ve <= vs || !y_scale) return min_delay;
    /* The programmed source pitch is already in pixels. Scale only the
     * output window, whose width is in video clocks and height in halflines. */
    uint32_t w = he > hs && x_scale ? (he - hs) * x_scale / 1024 : pitch;
    uint32_t h = (ve - vs) * y_scale / 2048;
    /* A doubled VI pitch scans alternate rows of an interlaced color image.
     * The geometry above counts one field, but SyncFull completes the whole
     * rendered frame. Y_SCALE-based interlace already counts both fields;
     * distinguish the two layouts using the native color-image pitch. */
    if (dp->instruction_timing && vi_uses_alternate_rows(vi, dp->timing.fb_width))
        h *= 2;
    uint32_t ticks = w * h * 2;
    return ticks > min_delay ? ticks : min_delay;
}

static void process_dpc_commands(struct rdp_core* dp, int from_rsp)
{
    uint32_t dp_pending;
    struct rdp_timing_sync sync = {0};
    if (dp->dpc_regs[DPC_STATUS_REG] & DPC_STATUS_FREEZE)
        return;
    if (dp->dpc_regs[DPC_CURRENT_REG] == dp->dpc_regs[DPC_END_REG])
        return;

    attach_timing(dp);
    dp_pending = dp->mi->regs[MI_INTR_REG] & MI_INTR_DP;
    if (dp->instruction_timing && (from_rsp || dp->vi_regs)) {
        int64_t submitted = dp->mi->r4300->cp0.count_clock;
#if defined(HAVE_PARALLEL_RSP)
        if (from_rsp) submitted = dp->sp->slice_start_clock +
            rsp_count_ticks(parallelRSPExecutedCycles(), dp->sp->slice_overhang);
#endif
        uint32_t begin = dp->dpc_regs[DPC_CURRENT_REG] & 0xfffff8;
        uint32_t end = dp->dpc_regs[DPC_END_REG] & 0xfffff8;
        for (uint32_t at = begin; at < end;) {
            uint32_t count = (end - at) / 4;
            const uint32_t *source;
            if (dp->dpc_regs[DPC_STATUS_REG] & DPC_STATUS_XBUS_DMEM_DMA) {
                uint32_t available = (4096 - (at & 4095)) / 4;
                if (count > available) count = available;
                source = dp->sp->mem + ((at & 4095) >> 2);
            } else if (at < 0x800000) {
                uint32_t available = (0x800000 - at) / 4;
                if (count > available) count = available;
                source = dp->fb.rdram->dram + (at >> 2);
            } else {
                const uint32_t zero = 0;
                rdp_timing_words_sync(&dp->timing, &zero, 1, submitted, &sync);
                at += 4;
                continue;
            }
            rdp_timing_words_sync(&dp->timing, source, count, submitted, &sync);
            at += count * 4;
        }
    }
    retire_counters(dp, dp->mi->r4300->cp0.count_clock * 4);
    framebuffer_prepare_gpu(&dp->fb);
    renderer_process_commands();
    protect_framebuffers(&dp->fb);
    dp->dpc_regs[DPC_STATUS_REG] |= DPC_STATUS_CBUF_READY;
    if (sync.seen && !dp->timing.pos && ((dp->timing.words[0] >> 24) & 63) == 0x29)
        dp->pipe_sync_deadline = dp->timing.deadline > 0 ? dp->timing.deadline : 1;

    /* Native RSP submissions can publish a decoded FullSync even when the
     * renderer has no completion signal. Preserve any latched DP interrupt. */
    const int decoded_rsp_fence = from_rsp && sync.seen;
#ifdef RDP_TIMING_OBSERVE_QUEUE
    if (sync.seen)
        RDP_TIMING_OBSERVE_QUEUE(0, dp->mi->r4300->cp0.count_clock,
            sync.deadline, dp->timing.deadline, dp_pending,
            get_event(&dp->mi->r4300->cp0.q, DP_INT) != NULL);
#endif
    if (decoded_rsp_fence || (!dp_pending && (dp->mi->regs[MI_INTR_REG] & MI_INTR_DP)))
    {
        if (from_rsp || dp->vi_regs)
        {
            /* Retain the LLE completion approximation at SyncFull, rather
             * than consuming any pending DP bit at graphics-task return. */
            if (!dp_pending)
                dp->mi->regs[MI_INTR_REG] &= ~MI_INTR_DP;
            if (!get_event(&dp->mi->r4300->cp0.q, DP_INT))
            {
                cp0_update_count(dp->mi->r4300);
                /* Use the decoded fence when available. It already includes
                 * packet delivery and excludes work after the first FullSync. */
                int64_t deadline = sync.seen ? sync.deadline : dp->timing.deadline;
                int64_t work = dp->instruction_timing ? deadline - dp->mi->r4300->cp0.count_clock : 0;
                uint64_t remaining = work > 0 ? (uint64_t)work : 0;
                if (!sync.seen) {
                    uint64_t fallback = rdp_sync_full_delay(dp);
#if defined(HAVE_PARALLEL_RSP)
                    if (from_rsp)
                        fallback += rsp_count_ticks(parallelRSPExecutedCycles(), 0);
#endif
                    if (remaining < fallback)
                        remaining = fallback;
                }
                uint32_t delay = remaining > UINT32_MAX ? UINT32_MAX : (uint32_t)remaining;
#ifndef N64EMU_USF
                /* Use the decoded deadline for one-cycle RSP fences with
                 * framebuffer reads disabled. Keep the scanout allowance
                 * for interlace that fetches alternating framebuffer rows. */
                int unadjusted_rsp_fence = decoded_rsp_fence && sync.one_cycle_without_image_read &&
                    !vi_uses_alternate_rows(dp->vi_regs, sync.fb_width);
                if (dp->instruction_timing && !dp->mi->r4300->cache_timing &&
                    !unadjusted_rsp_fence) {
                    /* Keep short fences close to the raster estimate.
                     * Ramp up the allowance past 1.6 VI fields.
                     * Bound the resulting delay by three fields while
                     * preserving any longer raster deadline. */
                    uint64_t estimated_delay = (uint64_t)delay * 5 / 4;
                    uint32_t field_ticks = dp->timing.vi_field_ticks;
                    if (field_ticks) {
                        uint64_t ramp_start = (uint64_t)field_ticks * 8 / 5;
                        uint64_t excess = delay > ramp_start ? delay - ramp_start : 0;
                        uint64_t ramped_delay = ((uint64_t)delay * 105 + excess * 175) / 100;
                        uint64_t field_limit = (uint64_t)field_ticks * 3;
                        if (estimated_delay > ramped_delay)
                            estimated_delay = ramped_delay;
                        if (estimated_delay > field_limit)
                            estimated_delay = delay > field_limit ? delay : field_limit;
                        /* For work within two fields, keep the estimated allowance
                         * from moving an on-time raster fence past a retrace. */
                        if (estimated_delay > delay && delay <= (uint64_t)field_ticks * 2) {
                            const struct cp0* cp0 = &dp->mi->r4300->cp0;
                            const unsigned int* vi_event = get_event(&cp0->q, VI_INT);
                            if (vi_event) {
                                uint32_t next_vi = *vi_event - cp0->regs[CP0_COUNT_REG];
                                if (next_vi <= field_ticks) {
                                    uint32_t until_vi = delay < next_vi ? next_vi - delay :
                                        field_ticks - (delay - next_vi) % field_ticks;
                                    if (estimated_delay - delay >= until_vi)
                                        estimated_delay = (uint64_t)delay + until_vi - 1;
                                }
                            }
                        }
                    }
                    /* Calibrate the additional allowance for progressive scanout
                     * with one-cycle antialiased framebuffer feedback. */
                    if (decoded_rsp_fence && sync.one_cycle_aa_image_read && dp->vi_regs &&
                        (dp->vi_regs[VI_STATUS_REG] & 0x42u) == 2u)
                        estimated_delay = delay + (estimated_delay - delay) * 97 / 100;
                    delay = estimated_delay > UINT32_MAX ? UINT32_MAX : (uint32_t)estimated_delay;
                }
#endif
                if (!dp_pending || delay)
                    add_interrupt_event(&dp->mi->r4300->cp0, DP_INT, delay);
            }
        }
        else
            signal_rcp_interrupt(dp->mi, MI_INTR_DP);
    }
}

static void update_dpc_status(struct rdp_core* dp, uint32_t w, int from_rsp)
{
    /* clear / set xbus_dmem_dma */
    if (w & DPC_CLR_XBUS_DMEM_DMA) dp->dpc_regs[DPC_STATUS_REG] &= ~DPC_STATUS_XBUS_DMEM_DMA;
    if (w & DPC_SET_XBUS_DMEM_DMA) dp->dpc_regs[DPC_STATUS_REG] |= DPC_STATUS_XBUS_DMEM_DMA;

    /* clear / set freeze */
    if (w & DPC_CLR_FREEZE)
    {
        dp->dpc_regs[DPC_STATUS_REG] &= ~DPC_STATUS_FREEZE;

        if (dp->do_on_unfreeze & DELAY_DP_INT)
            signal_rcp_interrupt(dp->mi, MI_INTR_DP);
        if (dp->do_on_unfreeze & DELAY_UPDATESCREEN)
            renderer_present();
        dp->do_on_unfreeze = 0;
#ifdef __LIBRETRO__
#endif
        process_dpc_commands(dp, from_rsp);
    }
    if (w & DPC_SET_FREEZE) dp->dpc_regs[DPC_STATUS_REG] |= DPC_STATUS_FREEZE;

    /* clear / set flush */
    if (w & DPC_CLR_FLUSH) dp->dpc_regs[DPC_STATUS_REG] &= ~DPC_STATUS_FLUSH;
    if (w & DPC_SET_FLUSH) dp->dpc_regs[DPC_STATUS_REG] |= DPC_STATUS_FLUSH;

    if (dp->instruction_timing && (w & (DPC_CLR_TMEM_CTR | DPC_CLR_PIPE_CTR | DPC_CLR_CMD_CTR))) {
        int64_t now = busy_now(dp, from_rsp);
        unsigned mask = ((w & DPC_CLR_CMD_CTR) ? 1 : 0) |
            ((w & DPC_CLR_PIPE_CTR) ? 2 : 0) | ((w & DPC_CLR_TMEM_CTR) ? 4 : 0);
        rdp_counters_clear(&dp->counters, now, mask);
        retire_counters(dp, now);
    }
    if (w & DPC_CLR_TMEM_CTR) dp->dpc_regs[DPC_TMEM_REG] = 0;
    if (w & DPC_CLR_PIPE_CTR) dp->dpc_regs[DPC_PIPEBUSY_REG] = 0;
    if (w & DPC_CLR_CMD_CTR) dp->dpc_regs[DPC_BUFBUSY_REG] = 0;

    /* clear clock counter */
    if (w & DPC_CLR_CLOCK_CTR)
    {
        const uint32_t* cp0_regs;

        cp0_update_count(dp->mi->r4300);
        cp0_regs = r4300_cp0_regs(&dp->mi->r4300->cp0);
        dp->clock_base = cp0_regs[CP0_COUNT_REG];
        dp->clock_offset = 0;
#if defined(HAVE_PARALLEL_RSP)
        if (from_rsp) {
            if (dp->instruction_timing)
                dp->clock_base += (uint32_t)(dp->sp->slice_start_clock - dp->mi->r4300->cp0.count_clock);
            dp->clock_offset = -parallelRSPExecutedCycles();
        }
#endif
        dp->dpc_regs[DPC_CLOCK_REG] = 0;
    }
}


void init_rdp(struct rdp_core* dp,
              struct rsp_core* sp,
              struct mi_controller* mi,
              struct memory* mem,
              struct rdram* rdram,
              struct r4300_core* r4300)
{
    dp->sp = sp;
    dp->mi = mi;
    dp->vi_regs = NULL;
    dp->instruction_timing = 1;
#ifdef __LIBRETRO__
    dp->instruction_timing = renderer_component_timing();
#endif

    init_fb(&dp->fb, mem, rdram, r4300);
    dp->fb.timing = &dp->timing;
}

void poweron_rdp(struct rdp_core* dp)
{
    memset(dp->dpc_regs, 0, DPC_REGS_COUNT*sizeof(uint32_t));
    memset(dp->dps_regs, 0, DPS_REGS_COUNT*sizeof(uint32_t));
    dp->dpc_regs[DPC_STATUS_REG] |= DPC_STATUS_START_GCLK;

    dp->do_on_unfreeze = 0;
    dp->clock_base = 0;
    dp->clock_offset = 0;
    dp->pipe_sync_deadline = 0;
    rdp_counters_reset(&dp->counters);
    memset(&dp->timing, 0, sizeof(dp->timing));
    dp->timing.right = dp->timing.bottom = 4095;

    poweron_fb(&dp->fb);
}


void read_dpc_regs(void* opaque, uint32_t address, uint32_t* value)
{
    struct rdp_core* dp = (struct rdp_core*)opaque;
    uint32_t reg = dpc_reg(address);

    if (reg == DPC_CLOCK_REG)
        dp->dpc_regs[DPC_CLOCK_REG] = dpc_clock_now(dp, 0);

    read_busy(dp, reg, 0);
    *value = dp->dpc_regs[reg];
#ifdef RDP_TIMING_OBSERVE_READ
    RDP_TIMING_OBSERVE_READ(reg, 0, dp->mi->r4300->cp0.count_clock,
        dp->timing.deadline, *value);
#endif
}

void read_rsp_dpc_regs(void* opaque, uint32_t address, uint32_t* value)
{
    struct rdp_core* dp = (struct rdp_core*)opaque;
    if (dpc_reg(address) == DPC_CLOCK_REG)
        dp->dpc_regs[DPC_CLOCK_REG] = dpc_clock_now(dp, 1);
    read_busy(dp, dpc_reg(address), 1);
    *value = dp->dpc_regs[dpc_reg(address)];
#if defined(RDP_TIMING_OBSERVE_READ) && defined(HAVE_PARALLEL_RSP)
    RDP_TIMING_OBSERVE_READ(dpc_reg(address), 1, dp->sp->slice_start_clock +
        rsp_count_ticks(parallelRSPExecutedCycles(), dp->sp->slice_overhang),
        dp->timing.deadline, *value);
#endif
}

static void write_dpc_regs_from(void* opaque, uint32_t address, uint32_t value, uint32_t mask, int from_rsp)
{
    struct rdp_core* dp = (struct rdp_core*)opaque;
    uint32_t reg = dpc_reg(address);

    switch(reg)
    {
    case DPC_STATUS_REG:
        update_dpc_status(dp, value & mask, from_rsp);
    case DPC_CURRENT_REG:
    case DPC_CLOCK_REG:
    case DPC_BUFBUSY_REG:
    case DPC_PIPEBUSY_REG:
    case DPC_TMEM_REG:
        return;
    }

    switch(reg)
    {
    case DPC_START_REG:
        if (!(dp->dpc_regs[DPC_STATUS_REG] & DPC_STATUS_START_VALID))
        {
            masked_write(&dp->dpc_regs[reg], value, mask);
            dp->dpc_regs[reg] &= 0x00fffff8;
        }
        dp->dpc_regs[DPC_STATUS_REG] |= DPC_STATUS_START_VALID;
        break;
    case DPC_END_REG:
        masked_write(&dp->dpc_regs[reg], value, mask);
        dp->dpc_regs[reg] &= 0x00fffff8;
        if (dp->dpc_regs[DPC_STATUS_REG] & DPC_STATUS_START_VALID)
        {
            dp->dpc_regs[DPC_CURRENT_REG] = dp->dpc_regs[DPC_START_REG];
            dp->dpc_regs[DPC_STATUS_REG] &= ~DPC_STATUS_START_VALID;
        }
        if (!(dp->dpc_regs[DPC_STATUS_REG] & DPC_STATUS_FREEZE)) {
            dp->dpc_regs[DPC_STATUS_REG] |= UINT32_C(0xa8);
            dp->pipe_sync_deadline = -1;
        }
        process_dpc_commands(dp, from_rsp);
        break;
    }
}

void write_dpc_regs(void* opaque, uint32_t address, uint32_t value, uint32_t mask)
{
    write_dpc_regs_from(opaque, address, value, mask, 0);
}

void write_rsp_dpc_regs(void* opaque, uint32_t address, uint32_t value, uint32_t mask)
{
    write_dpc_regs_from(opaque, address, value, mask, 1);
}

void read_dps_regs(void* opaque, uint32_t address, uint32_t* value)
{
    struct rdp_core* dp = (struct rdp_core*)opaque;
    uint32_t reg = dps_reg(address);

    *value = dp->dps_regs[reg];
}

void write_dps_regs(void* opaque, uint32_t address, uint32_t value, uint32_t mask)
{
    struct rdp_core* dp = (struct rdp_core*)opaque;
    uint32_t reg = dps_reg(address);

    masked_write(&dp->dps_regs[reg], value, mask);
}

void rdp_interrupt_event(void* opaque)
{
    struct rdp_core* dp = (struct rdp_core*)opaque;

#ifdef RDP_TIMING_OBSERVE_QUEUE
    RDP_TIMING_OBSERVE_QUEUE(1, dp->mi->r4300->cp0.count_clock,
        0, dp->timing.deadline, dp->mi->regs[MI_INTR_REG] & MI_INTR_DP, 0);
#endif
    raise_rcp_interrupt(dp->mi, MI_INTR_DP);
}

