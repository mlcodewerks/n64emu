/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * *
 *   Mupen64plus-rsp-hle - alist_nead.c                                    *
 *   Mupen64Plus homepage: https://mupen64plus.org/                        *
 *   Copyright (C) 2014 Bobby Smiles                                       *
 *   Copyright (C) 2009 Richard Goedeken                                   *
 *   Copyright (C) 2002 Hacktarux                                          *
 *                                                                         *
 *   This program is free software; you can redistribute it and/or modify  *
 *   it under the terms of the GNU General Public License as published by  *
 *   the Free Software Foundation; either version 2 of the License, or     *
 *   (at your option) any later version.                                   *
 *                                                                         *
 *   This program is distributed in the hope that it will be useful,       *
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of        *
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the         *
 *   GNU General Public License for more details.                          *
 *                                                                         *
 *   You should have received a copy of the GNU General Public License     *
 *   along with this program; if not, write to the                         *
 *   Free Software Foundation, Inc.,                                       *
 *   51 Franklin Street, Fifth Floor, Boston, MA 02110-1301, USA.          *
 * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * */

#include <boolean.h>
#include <stdint.h>
#include <string.h>

#include "alist.h"
#include "common.h"
#include "hle_external.h"
#include "hle_internal.h"
#include "memory.h"
#include "ucodes.h"
#include "hle_timing.h"
#include "hle_timing_nead.h"

/* remove windows define to 0x06 */
#ifdef DUPLICATE
#undef DUPLICATE
#endif

/* audio commands definition */
/* The nead microcodes treat $v31 as a scratch constant register that
 * several handlers reload and one corrupts: ADDMIXER opens with
 * "vaddc $v31, $v31, $v31" - the author's VCO-clear idiom - which sets
 * the carry flags from v31's current content, doubles v31, and the
 * first vadd batch of the add loop then consumes those carries.  With
 * v31 freshly loaded by a preceding MIXER or ADPCM (the constant
 * vector, lane 3 = 0xffff) the "clear" injects a +1 into sample 3 of
 * the first eight samples; RESAMPLE leaves the all-ones vector (no
 * carries), and back-to-back ADDMIXERs see the doubled image
 * ([3,6,7], then onward as the doubling evolves).  v31 and the
 * consumed carries are real machine state that persists across
 * commands and tasks, so they are modelled here.  Verified against
 * cxd4 running the Star Fox 64 build: handler disassembly at text
 * +0xdf8 (vaddc), +0xaac / +0x24c (const reloads), +0x5b0 (resample
 * reload from data+0x70), and carry-signature probes for every
 * predecessor. */
#define nead_v31 (hle->nead_v31)

/* DMEM address of the state-staging slab ($23 in the microcode);
 * differs per build: 0xf90 (sf/sfj/wrjb), 0xfa0 (mk), 0xfb0
 * (oot/mm/ac lineage), 0xfc0 (ys/1080/fz - overlapping the OSTask,
 * whose fields the resample save then ships out as residue). */
static uint16_t nead_slab = 0xfb0;

/* Some revisions place the audio buffers above a fixed DMEM base and
 * the alist's buffer operands are relative to it; the microcode adds
 * the base when it uses them.  Scratch the microcode addresses
 * directly - the command staging area, the codebook, the state slab -
 * is not affected. */
static uint16_t nead_dmem_base = 0;

/* the Mario Kart build carries the aspMain-layout resample (back
 * distance and aligned-block state) rather than the later builds'
 * residue-carrying one */
static int nead_resample_old = 0;

/* per-build ENVMIXER dialect */
enum { NEAD_ENV_SF, NEAD_ENV_FZ, NEAD_ENV_OOT, NEAD_ENV_MK };
static int nead_env_style = NEAD_ENV_OOT;

/* DMEM address the codebook DMA lands at; the book lives in real DMEM
 * (not just the HLE-side table), so the shadow must carry it too */
static uint16_t nead_book = 0x330;

/* DMEM address at which the handler keeps SETBUFF's operands, or zero
 * where that staging has not been established */
static uint16_t nead_setbuff_store = 0;
static uint16_t nead_loop_offset = 8;

static uint32_t nead_address(struct hle_t* hle, uint32_t w2)
{
    return nead_dmem_base ? (alist_nead_segment_address(hle, w2) & 0x7fffffu) : (w2 & 0xffffffu);
}

/* whether FILTER averages the fresh coefficients with the saved table
 * (Ocarina lineage) or uses them straight (Shindou/Yoshi/1080) */
static int nead_filter_avg = 1;
static uint32_t nead_timing_profile;
/* Complete WRJB timing is admitted only for a verified fresh, one-chunk
 * command list. Unknown commands retain the coarse model. Its fitted DMA
 * coefficient compensates for startup, so replacing only RESAMPLE worsens
 * some complete-task estimates. Keep its handler evidence separate until
 * every command in the current list has a verified cost. */
static uint32_t nead_last_resample_clocks;
static uint32_t nead_wave_expected, nead_wave_observed;
static uint64_t nead_wave_clocks;
static int nead_wave_valid;
static uint32_t nead_dma_list_clocks;

/* Independent native handler/dispatch spans, including startup and final
 * return. Only bounded LOAD/SAVE lists whose complete original data image
 * is unchanged qualify. This does not replace other lists' coarse costs. */
static void nead_dma_list_begin(struct hle_t* hle)
{
    static const uint16_t base[7] = {189,193,137,797,732,173,193};
    static const uint8_t load_cpu[7] = {45,45,36,39,39,36,45};
    static const uint8_t save_cpu[7] = {44,44,35,42,42,35,44};
    static const uint64_t data_hash[7] = {
        UINT64_C(0xaff90ce9ce357ba3), UINT64_C(0xb4f360741b887a27),
        UINT64_C(0xd944ae8112835093), UINT64_C(0xf10141b60f9b7580),
        UINT64_C(0xe8761b5b7649d568), UINT64_C(0x77d711ac4ae96b7f),
        UINT64_C(0x98ceb4f938591947)
    };
    uint32_t profile = nead_timing_profile, bytes = *dmem_u32(hle, TASK_DATA_SIZE);
    uint32_t list = *dmem_u32(hle, TASK_DATA_PTR) & 0x7fffffu;
    uint32_t data = *dmem_u32(hle, TASK_UCODE_DATA) & 0x7fffffu;
    uint32_t data_size, rounded, boot, text, clocks, i;
    nead_dma_list_clocks = 0;
    if (!profile || profile > 7u || !bytes || (bytes & 7u) || (list & 7u) ||
        bytes > ((profile >= 3u && profile <= 5u) ? 128u : 64u) ||
        list > 0x800000u - bytes || *dmem_u32(hle, TASK_FLAGS) ||
        *hle->sp_dma_full || *hle->sp_dma_busy || *hle->sp_semaphore ||
        (*hle->sp_status & 0x80u) || (*hle->dpc_status & 1u)) return;
    data_size = profile == 4u || profile == 5u ? 0x330u : 0x2dfu;
    rounded = (data_size + 7u) & ~7u;
    if (*dmem_u32(hle, TASK_UCODE_DATA_SIZE) != data_size || (data & 7u) ||
        data > 0x800000u - rounded ||
        hle_timing_word_fingerprint((const uint32_t*)(hle->dram + data), rounded / 4u) !=
            data_hash[profile - 1u]) return;
    boot = *dmem_u32(hle, TASK_UCODE_BOOT) & 0x7fffffu;
    text = *dmem_u32(hle, TASK_UCODE) & 0x7fffffu;
    if (profile == 4u || profile == 5u) {
        if (*dmem_u32(hle, TASK_UCODE_BOOT_SIZE) != 0xd0u || (boot & 3u) ||
            boot > 0x800000u - 0xd0u ||
            hle_timing_word_fingerprint((const uint32_t*)(hle->dram + boot), 52u) !=
                UINT64_C(0x0f7c8dfaf9f62fc0)) return;
    }
    clocks = base[profile - 1u] + (profile == 3u ? 40u : 20u) * (bytes / 8u);
    for (i = 0; i < bytes; i += 8u) {
        uint32_t w1 = *dram_u32(hle, list + i), w2 = *dram_u32(hle, list + i + 4u);
        uint32_t opcode = w1 >> 24, count = (w1 >> 12) & 0xff0u;
        uint32_t address = (w2 & 0xffffffu) & ~7u;
        uint32_t at = w1 & 0xff8u, span;
        int early = (profile == 3u || profile == 6u) && !count;
        if (opcode != 0x14u && opcode != 0x15u) return;
        if (early) { clocks += 9u; continue; }
        if (opcode == 0x14u) {
            /* Code, dispatch and the current staged list must remain intact.
             * Zero-length LOAD encodes a large DMA and is not admitted. */
            if (!count || (w1 & 0x1000u) || at < 0x400u || at + count > 0xf90u ||
                address > 0x800000u - count) return;
            clocks += load_cpu[profile - 1u] + count / 8u;
        } else {
            span = count ? count : 4096u * 256u + 4088u * 255u;
            if (address > 0x800000u - span ||
                (address < list + bytes && list < address + span) ||
                (address < data + rounded && data < address + span) ||
                (address < text + 4096u && text < address + span) ||
                ((profile == 4u || profile == 5u) &&
                 address < boot + 0xd0u && boot < address + span)) return;
            clocks += save_cpu[profile - 1u] + (count ? count / 8u : 131072u);
        }
    }
    nead_dma_list_clocks = clocks;
}

static void nead_walk(struct hle_t* hle, const acmd_callback_t* abi,
                      unsigned count, uint32_t staging, uint32_t chunk)
{
    alist_nead_walk(hle, abi, count, staging, chunk);
    if (nead_dma_list_clocks && !nead_dmem_base)
        hle_timing_override(nead_dma_list_clocks);
}


static int16_t nead_clamp_s16(int_fast32_t x)
{
    x = (x < INT16_MIN) ? INT16_MIN : x;
    x = (x > INT16_MAX) ? INT16_MAX : x;
    return (int16_t)x;
}

static int16_t* nead_s16(struct hle_t* hle, uint16_t dmem)
{
    return (int16_t*)(hle->alist_buffer + ((dmem ^ S16) & 0xfff));
}

/* The alist buffer shadows the sample area of DMEM; the slab sits in
 * the task-header region, whose real content (OSTask fields for the
 * 0xfc0 builds, zeros otherwise) the resample save ships out as
 * residue.  Seed the shadow from real DMEM at task start; from then
 * on only the resample state load/save updates it. */
static void nead_slab_seed(struct hle_t* hle)
{
    unsigned k;
    for (k = 0; k < 0x20; k += 2)
        *nead_s16(hle, (uint16_t)(nead_slab + k)) =
            (int16_t)((*dmem_u32(hle, (nead_slab + k) & ~3)
                       >> (((nead_slab + k) & 2) ? 0 : 16)) & 0xffff);
}

static void nead_seed(struct hle_t* hle, uint32_t pc)
{
    uint32_t k, text = *dmem_u32(hle, TASK_UCODE);
    uint32_t data = *dmem_u32(hle, TASK_UCODE_DATA);
    uint32_t size = *dmem_u32(hle, TASK_UCODE_DATA_SIZE);
    /* Areas outside the data DMA retain the previous task's DMEM. */
    memcpy(hle->alist_buffer, hle->dmem, sizeof(hle->alist_buffer));
    nead_slab_seed(hle);
    /* Constant tables and command scratch are real DMEM, including tables
     * that the multiply/index commands can address without LOADBUFF. */
    alist_dmem_dma(hle, 0, data, size - 1u, 0);
    if (pc) {
        uint32_t boot = *dmem_u32(hle, TASK_UCODE_BOOT);
        for (k = 0; k < pc; ++k)
            hle->imem[k ^ S8] = hle->dram[((boot + k) & 0x7fffffu) ^ S8];
    }
    for (k = pc; k < 4096; ++k)
        hle->imem[k ^ S8] = hle->dram[((text + k - pc) & 0x7fffffu) ^ S8];
    nead_timing_profile = hle_timing_nead_profile((const uint32_t *)hle->imem);
    nead_dma_list_begin(hle);
}


static void nead_v31_load(struct hle_t* hle, uint16_t data_off)
{
    uint32_t ucode_data = *dmem_u32(hle, TASK_UCODE_DATA);
    unsigned i;
    for (i = 0; i < 8; ++i)
        nead_v31[i] = *dram_u16(hle, ucode_data + data_off + 2*i);
}

static void SPNOOP(struct hle_t* UNUSED(hle), uint32_t UNUSED(w1), uint32_t UNUSED(w2))
{
}
static void NEAD_00(struct hle_t* hle, uint32_t w1, uint32_t w2)
{
    alist_nead_report(hle, w1, w2, nead_slab);
}

static void LOADADPCM(struct hle_t* hle, uint32_t w1, uint32_t w2)
{
    uint16_t count   = w1;
    uint32_t address = nead_address(hle, w2);

    {  size_t entries = count >> 1;
        const size_t cap = sizeof(hle->alist_nead.table)/sizeof(hle->alist_nead.table[0]);
        if (entries > cap) entries = cap;   /* count is unbounded alist data */
        dram_load_u16(hle, (uint16_t*)hle->alist_nead.table, address, entries);
    }
    if (nead_book != 0) {
        /* the DMA also lands the raw book in DMEM; mirror it so saves
         * from that region ship the same bytes.  (Not for Mario Kart:
         * its uniformly 0x450-biased address space puts the book below
         * anything the shadow can express or a game can address.) */
        unsigned k;
        uint16_t n = (uint16_t)(w1 & 0xffff);
        for (k = 0; k + 1 < n; k += 2)
            *nead_s16(hle, (uint16_t)(nead_book + k)) =
                (int16_t)*dram_u16(hle, address + k);
    }

}

static void SETLOOP(struct hle_t* hle, uint32_t UNUSED(w1), uint32_t w2)
{
    hle->alist_nead.loop = nead_dmem_base ? alist_nead_segment_address(hle, w2) : w2 & 0xffffffu;
    if (nead_setbuff_store) {
        *nead_s16(hle, nead_setbuff_store+nead_loop_offset) = (int16_t)(hle->alist_nead.loop >> 16);
        *nead_s16(hle, nead_setbuff_store+nead_loop_offset+2u) = (int16_t)hle->alist_nead.loop;
    }
}

static void SETBUFF(struct hle_t* hle, uint32_t w1, uint32_t w2)
{
    if (nead_dmem_base && (w1 & 0x80000u)) {
        *nead_s16(hle, nead_setbuff_store+10u) = (int16_t)(w1+nead_dmem_base);
        *nead_s16(hle, nead_setbuff_store+12u) = (int16_t)((w2>>16)+nead_dmem_base);
        *nead_s16(hle, nead_setbuff_store+14u) = (int16_t)(w2+nead_dmem_base);
        return;
    }
    hle->alist_nead.in    = w1 + nead_dmem_base;
    hle->alist_nead.out   = (w2 >> 16) + nead_dmem_base;
    hle->alist_nead.count = w2;

    /* the handler keeps the three values in DMEM as well, biased the
     * same way it uses them; a buffer walk that overruns reads them */
    if (nead_setbuff_store != 0) {
        *nead_s16(hle, (uint16_t)(nead_setbuff_store    )) = (int16_t)hle->alist_nead.in;
        *nead_s16(hle, (uint16_t)(nead_setbuff_store + 2)) = (int16_t)hle->alist_nead.out;
        *nead_s16(hle, (uint16_t)(nead_setbuff_store + 4)) = (int16_t)hle->alist_nead.count;
    }
}

static void ADPCM(struct hle_t* hle, uint32_t w1, uint32_t w2)
{
    uint8_t  flags   = (w1 >> 16);
    uint32_t address = nead_address(hle, w2);
    uint32_t frames = ((uint32_t)hle->alist_nead.count + 31u) / 32u;
    uint32_t input = hle->alist_nead.in, output = hle->alist_nead.out;
    uint32_t step = flags & 4u ? 5u : 9u;
    uint32_t loop_clocks = 0;
    if (nead_timing_profile == 1u && frames && frames <= 127u &&
        nead_setbuff_store == 0x2e0u && nead_dmem_base == 0 && !(output & 15u) &&
        input + frames * step + 9u <= 4096u && output + 32u * (frames + 1u) <= 4096u &&
        (input >= output + 32u * (frames + 1u) || input + frames * step + 9u <= output) &&
        (output >= 0x530u || output + 32u * (frames + 1u) <= 0x330u) &&
        (uint16_t)*nead_s16(hle, 0x12u) == 0x1230u &&
        (uint16_t)*nead_s16(hle, 0x2e0u) == input &&
        (uint16_t)*nead_s16(hle, 0x2e2u) == output &&
        (uint16_t)*nead_s16(hle, 0x2e4u) == hle->alist_nead.count) {
        uint32_t special = 0, n;
        for (n = 1; n <= frames; ++n)
            special += (hle->alist_buffer[(input + n * step) ^ S8] >> 4) == (flags & 4u ? 14u : 12u);
        loop_clocks = hle_timing_nead_adpcm(frames, flags, special);
    }

    nead_v31_load(hle, 0x00);
    alist_adpcm(
            hle,
            flags & 0x1,
            flags & 0x2,
            flags & 0x4,
            hle->alist_nead.out,
            hle->alist_nead.in,
            (hle->alist_nead.count + 0x1f) & ~0x1f,
            hle->alist_nead.table,
            hle->alist_nead.loop,
            address, 0);
    if (loop_clocks) hle_timing_refine(HLE_WORK_ADPCM, frames, loop_clocks);
}

static void CLEARBUFF(struct hle_t* hle, uint32_t w1, uint32_t w2)
{
    uint16_t dmem  = w1 + nead_dmem_base;
    uint32_t count = w2 & 0xffffu;

    if (count == 0)
        return;

    /* Two SDVs precede the loop test in each NEAD revision: a partial
     * last block still clears sixteen bytes, with DMEM address wrapping.
     * Keep the rounded 65536-byte extent wider than a halfword. */
    alist_clear(hle, dmem, (count + 15u) & ~15u);
}

static void LOADBUFF(struct hle_t* hle, uint32_t w1, uint32_t w2)
{
    uint16_t count   = (w1 >> 12) & 0xfff;
    uint16_t dmem    = (w1 & 0xfff) + nead_dmem_base;
    uint32_t address = nead_address(hle, w2);

    /* All seven original handlers discard the low four count bits.
     * Keep the native zero-count early return in the AC/MM-debug builds;
     * large zero LOADs and self-modifying IMEM loads need native execution. */
    if (nead_timing_profile >= 1u && nead_timing_profile <= 7u) {
        count &= 0xff0u;
        if (!count && (nead_timing_profile == 3u || nead_timing_profile == 6u)) return;
        if (count && address > 0x800000u - count) {
            alist_dmem_dma(hle, w1 & 0xffffu, address, (uint32_t)count - 1u, 0);
            return;
        }
    }

    if (nead_dmem_base)
        alist_dmem_dma(hle, (w1 & 0xffffu) + nead_dmem_base,
            alist_nead_segment_address(hle, w2), ((w1 >> 12) & 0xff0u) - 1u, 0);
    else
        alist_load(hle, dmem, address, count);
}

static void SAVEBUFF(struct hle_t* hle, uint32_t w1, uint32_t w2)
{
    uint16_t count   = (w1 >> 12) & 0xfff;
    uint16_t dmem    = (w1 & 0xfff) + nead_dmem_base;
    uint32_t address = nead_address(hle, w2);

    if (nead_timing_profile >= 1u && nead_timing_profile <= 7u) {
        /* Original handlers mask the byte count to 0xff0. The discarded
         * low count bits remain in SP_MEM_ADDR and can select IMEM.
         * Animal Forest and MM debug return on a zero masked count;
         * the other five decrement zero into the full length/row/skip encoding.
         * Keep ordinary DMEM transfers on the existing block-copy path. */
        count &= 0xff0u;
        if (!count && (nead_timing_profile == 3u || nead_timing_profile == 6u)) return;
        if (!count || (w1 & 0x1000u) || address > 0x800000u - count) {
            alist_dmem_dma(hle, w1 & 0xffffu, address, (uint32_t)count - 1u, 1);
            return;
        }
    }

    if (nead_dmem_base)
        alist_dmem_dma(hle, (w1 & 0xffffu) + nead_dmem_base,
            alist_nead_segment_address(hle, w2), ((w1 >> 12) & 0xff0u) - 1u, 1);
    else
        alist_save(hle, dmem, address, count);
}

static void MIXER(struct hle_t* hle, uint32_t w1, uint32_t w2)
{
    uint16_t count = (w1 >> 12) & 0xff0;
    int16_t  gain  = w1;
    uint16_t dmemi = (w2 >> 16) + nead_dmem_base;
    uint16_t dmemo = (uint16_t)w2 + nead_dmem_base;

    nead_v31_load(hle, 0x00);
    /* the mix loop processes two 8-sample batches per iteration and
     * only then tests the counter, so the extent rounds up to 0x20 */
    alist_mix_nead(hle, dmemo, dmemi, (uint16_t)((count + 0x1f) & ~0x1f), gain);
}


static void RESAMPLE(struct hle_t* hle, uint32_t w1, uint32_t w2)
{
    uint8_t  flags   = (w1 >> 16);
    uint16_t pitch   = w1;
    uint32_t address = nead_address(hle, w2);
    uint32_t count = ((uint32_t)hle->alist_nead.count + 15u) & ~15u;
    uint32_t exact_clocks = 0;
    if (!count && nead_timing_profile >= 1u && nead_timing_profile <= 7u) {
        static const uint16_t targets[7] = {
            0x14f4u, 0x1514u, 0x147cu, 0x1568u, 0x1534u, 0x14bcu, 0x14fcu
        };
        uint32_t input = hle->alist_nead.in, output = hle->alist_nead.out;
        uint32_t params = nead_timing_profile == 4u || nead_timing_profile == 5u ? 0x320u : 0x2e0u;
        uint16_t target = targets[nead_timing_profile - 1u];
        if (nead_setbuff_store == params && !nead_dmem_base &&
            input >= 0x400u && input <= 0xf88u && output >= 0x400u &&
            output + 16u <= nead_slab && !(output & 15u) &&
            (input >= output + 32u || input + 40u <= output) &&
            (uint16_t)*nead_s16(hle, 0x1au) == target &&
            (uint16_t)*nead_s16(hle, params) == input &&
            (uint16_t)*nead_s16(hle, params + 2u) == output &&
            (uint16_t)*nead_s16(hle, params + 4u) == 0) {
            /* These original loops test count after storing eight samples.
             * A zero count still executes one group and advances state. */
            count = 16u;
        }
    }

    if (nead_timing_profile >= 1u && nead_timing_profile <= 7u && flags <= 7u &&
        count && count <= 127u * 16u && !nead_dmem_base &&
        !*hle->sp_dma_full && !*hle->sp_dma_busy && !*hle->sp_semaphore) {
        static const uint16_t targets[7] = {
            0x14f4u, 0x1514u, 0x147cu, 0x1568u, 0x1534u, 0x14bcu, 0x14fcu
        };
        uint32_t params = nead_timing_profile == 4u || nead_timing_profile == 5u ? 0x320u : 0x2e0u;
        uint32_t input = hle->alist_nead.in, output = hle->alist_nead.out;
        if (nead_setbuff_store == params && input >= 0x400u && input + 2u * count + 40u <= 4096u &&
            output >= 0x400u && output + count <= nead_slab && !(output & 15u) &&
            (input >= output + count + 16u || input + 2u * count + 40u <= output) &&
            (uint16_t)*nead_s16(hle, 0x1au) == targets[nead_timing_profile - 1u] &&
            (uint16_t)*nead_s16(hle, params) == input &&
            (uint16_t)*nead_s16(hle, params + 2u) == output &&
            (uint16_t)*nead_s16(hle, params + 4u) == hle->alist_nead.count)
            exact_clocks = hle_timing_nead_resample(nead_timing_profile, count / 16u, flags);
    }

    nead_last_resample_clocks = exact_clocks;
    nead_v31_load(hle, 0x70);
    if (nead_resample_old) {
        alist_resample_audio(
                hle,
                nead_slab,
                flags & 0x1,
                flags & 0x2,
                hle->alist_nead.out,
                hle->alist_nead.in,
                count,
                pitch << 1,
                address);
        if (exact_clocks && nead_timing_profile != 5u)
            hle_timing_refine(HLE_WORK_RESAMPLE, count / 16u, exact_clocks);
        return;
    }
    alist_resample_nead(
            hle,
            nead_slab,
            flags,
            hle->alist_nead.out,
            hle->alist_nead.in,
            count,
            pitch << 1,
            address);
    if (exact_clocks && nead_timing_profile != 5u)
        hle_timing_refine(HLE_WORK_RESAMPLE, count / 16u, exact_clocks);
}

static void RESAMPLE_ZOH(struct hle_t* hle, uint32_t w1, uint32_t w2)
{
    uint16_t pitch      = w1;
    uint16_t pitch_accu = w2;

    alist_resample_zoh(
            hle,
            hle->alist_nead.out,
            hle->alist_nead.in,
            hle->alist_nead.count,
            (uint32_t)pitch << 2,
            pitch_accu);
}

static void DMEMMOVE(struct hle_t* hle, uint32_t w1, uint32_t w2)
{
    uint16_t dmemi = w1 + nead_dmem_base;
    uint16_t dmemo = (w2 >> 16) + nead_dmem_base;
    uint16_t count = w2;

    if (count == 0)
        return;

    /* the move loop copies whole 16-byte rows, so the extent rounds up
     * to 0x10 rather than to a word */
    alist_move(hle, dmemo, dmemi, (count + 0xf) & ~0xf);
}

static void ENVSETUP1_MK(struct hle_t* hle, uint32_t w1, uint32_t w2)
{
    hle->alist_nead.env_values[2] = (w1 >> 8) & 0xff00;
    hle->alist_nead.env_steps[2]  = 0;
    hle->alist_nead.env_steps[0]  = (w2 >> 16);
    hle->alist_nead.env_steps[1]  = w2;
}

static void ENVSETUP1(struct hle_t* hle, uint32_t w1, uint32_t w2)
{
    /* the handler clears the whole envelope vector before writing the
     * wet lanes, so the dry envelopes are zero until ENVSETUP2 */
    hle->alist_nead.env_values[0] = 0;
    hle->alist_nead.env_values[1] = 0;
    hle->alist_nead.env_values[2] = (w1 >> 8) & 0xff00;
    hle->alist_nead.env_steps[2]  = w1;
    hle->alist_nead.env_steps[0]  = (w2 >> 16);
    hle->alist_nead.env_steps[1]  = w2;
    if (nead_timing_profile >= 1u && nead_timing_profile <= 3u) {
        memset(hle->rsp_nead_env, 0, sizeof(hle->rsp_nead_env));
        hle->rsp_nead_env[4] = hle->alist_nead.env_values[2];
        hle->rsp_nead_env[5] = hle->rsp_nead_env[4] + hle->alist_nead.env_steps[2];
        hle->rsp_nead_env_written = 1;
    }
}

static void ENVSETUP2(struct hle_t* hle, uint32_t UNUSED(w1), uint32_t w2)
{
    hle->alist_nead.env_values[0] = (w2 >> 16);
    hle->alist_nead.env_values[1] = w2;
    if (nead_timing_profile >= 1u && nead_timing_profile <= 3u) {
        hle->rsp_nead_env[0] = hle->alist_nead.env_values[0];
        hle->rsp_nead_env[1] = hle->rsp_nead_env[0] + hle->alist_nead.env_steps[0];
        hle->rsp_nead_env[2] = hle->alist_nead.env_values[1];
        hle->rsp_nead_env[3] = hle->rsp_nead_env[2] + hle->alist_nead.env_steps[1];
        hle->rsp_nead_env_written = 1;
    }
}

static void ENVMIXER_MK(struct hle_t* hle, uint32_t w1, uint32_t w2)
{
    int16_t xors[4];

    uint16_t dmemi = ((w1 >> 12) & 0xff0) + nead_dmem_base;
    uint8_t  count = (w1 >>  8) & 0xff;
    uint16_t dmem_dl = ((w2 >> 20) & 0xff0) + nead_dmem_base;
    uint16_t dmem_dr = ((w2 >> 12) & 0xff0) + nead_dmem_base;
    uint16_t dmem_wl = ((w2 >>  4) & 0xff0) + nead_dmem_base;
    uint16_t dmem_wr = ((w2 <<  4) & 0xff0) + nead_dmem_base;

    xors[2] = 0;    /* unsupported by this ucode */
    xors[3] = 0;    /* unsupported by this ucode */
    xors[0] = 0 - (int16_t)((w1 & 0x2) >> 1);
    xors[1] = 0 - (int16_t)((w1 & 0x1)     );

    alist_envmix_nead(
            hle,
            false,  /* no swap flag in this build */
            dmem_dl, dmem_dr,
            dmem_wl, dmem_wr,
            dmemi, count,
            hle->alist_nead.env_values,
            hle->alist_nead.env_steps,
            xors, 8, false, NULL);
}

static void ENVMIXER(struct hle_t* hle, uint32_t w1, uint32_t w2)
{
    int16_t xors[4];

    uint16_t dmemi = (w1 >> 12) & 0xff0;
    unsigned count = (w1 >> 8) & 0xff;
    uint32_t exact_clocks = 0;
    bool     swap_wet_LR;
    bool     wet_bypass = false;
    uint16_t dmem_dl = (w2 >> 20) & 0xff0;
    uint16_t dmem_dr = (w2 >> 12) & 0xff0;
    uint16_t dmem_wl = (w2 >>  4) & 0xff0;
    uint16_t dmem_wr = (w2 <<  4) & 0xff0;

    if (nead_timing_profile >= 1u && nead_timing_profile <= 3u &&
        nead_dmem_base == 0 && nead_env_style == NEAD_ENV_OOT) {
        static const uint16_t target[3] = { 0x1884u, 0x18bcu, 0x1824u };
        uint32_t addresses[5] = { dmemi, dmem_dl, dmem_dr, dmem_wl, dmem_wr };
        uint32_t bytes = (count ? (count + 15u) & ~15u : 16u) * 2u;
        unsigned i, j;
        int valid = (uint16_t)*nead_s16(hle, 0x36u) == target[nead_timing_profile - 1u];
        for (i = 0; i < 5; ++i) {
            if (addresses[i] < 0x400u || addresses[i] + bytes > 0xf80u) valid = 0;
            for (j = 0; j < i; ++j)
                if (addresses[i] < addresses[j] + bytes && addresses[j] < addresses[i] + bytes) valid = 0;
        }
        if (valid) {
            exact_clocks = hle_timing_nead_envmix(count, w1);
            /* This loop is post-tested in all three identified programs. */
            if (!count) count = 16u;
        }
    }

    if (nead_env_style == NEAD_ENV_FZ) {
        /* the mask table loads survive in this build but the loop
         * never applies them - bits 0 and 1 are inert */
        xors[0] = 0;
        xors[1] = 0;
    }
    else {
        xors[0] = 0 - (int16_t)((w1 & 0x2) >> 1);
        xors[1] = 0 - (int16_t)((w1 & 0x1)     );
    }
    if (nead_env_style == NEAD_ENV_OOT) {
        /* four masks, swap on bit 4 */
        xors[2] = 0 - (int16_t)((w1 & 0x8) >> 1);
        xors[3] = 0 - (int16_t)((w1 & 0x4) >> 1);
        swap_wet_LR = (w1 >> 4) & 0x1;
    }
    else {
        /* two masks, swap on bit 2; F-Zero X additionally skips the
         * wet writes when the second wet envelope lane is zero */
        xors[2] = 0;
        xors[3] = 0;
        swap_wet_LR = (w1 >> 2) & 0x1;
        wet_bypass = (nead_env_style == NEAD_ENV_FZ);
    }

    if (exact_clocks) {
        unsigned i;
        for (i = 0; i < 3; ++i) hle->alist_nead.env_steps[i] *= 2u;
        hle->rsp_nead_env_written = 1;
    }
    alist_envmix_nead(
            hle,
            swap_wet_LR,
            dmem_dl, dmem_dr,
            dmem_wl, dmem_wr,
            dmemi, count,
            hle->alist_nead.env_values,
            hle->alist_nead.env_steps,
            xors, 16, wet_bypass, exact_clocks ? hle->rsp_nead_env : NULL);
    if (exact_clocks)
        hle_timing_refine(HLE_WORK_ENVMIX, (count + 7u) / 8u, exact_clocks);
}

static void DUPLICATE(struct hle_t* hle, uint32_t w1, uint32_t w2)
{
    uint8_t  count = (w1 >> 16);
    uint16_t dmemi = w1;
    uint16_t dmemo = (w2 >> 16);

    alist_repeat64(hle, dmemo, dmemi, count);
}

static void INTERL(struct hle_t* hle, uint32_t w1, uint32_t w2)
{
    uint16_t count = w1;
    uint16_t dmemi = (w2 >> 16) + nead_dmem_base;
    uint16_t dmemo = (uint16_t)w2 + nead_dmem_base;

    alist_copy_every_other_sample(hle, dmemo, dmemi, count);
}

static void INTERLEAVE_MK(struct hle_t* hle, uint32_t UNUSED(w1), uint32_t w2)
{
    uint16_t left = (w2 >> 16) + nead_dmem_base;
    uint16_t right = (uint16_t)w2 + nead_dmem_base;

    if (hle->alist_nead.count == 0)
        return;

    alist_interleave(hle, hle->alist_nead.out, left, right, hle->alist_nead.count);
}

static void INTERLEAVE(struct hle_t* hle, uint32_t w1, uint32_t w2)
{
    uint16_t count = ((w1 >> 12) & 0xff0);
    uint16_t dmemo = w1;
    uint16_t left = (w2 >> 16);
    uint16_t right = w2;

    alist_interleave(hle, dmemo, left, right, count);
}

static void ADDMIXER(struct hle_t* hle, uint32_t w1, uint32_t w2)
{
    uint16_t count = (w1 >> 12) & 0xff0;
    uint16_t dmemi = (w2 >> 16);
    uint16_t dmemo = w2;
    uint8_t carry[8];
    unsigned i;

    /* the entry vaddc: VCO = carry-out of v31 + v31, and v31 doubles */
    for (i = 0; i < 8; ++i) {
        carry[i] = (uint8_t)(((uint32_t)nead_v31[i] * 2) >> 16);
        nead_v31[i] = (uint16_t)(nead_v31[i] << 1);
    }

    /* four 8-sample batches per iteration, counter tested after, so
     * the extent rounds up to 0x40; the first vadd batch consumes the
     * carries and clears them */
    count = (uint16_t)((count + 0x3f) & ~0x3f);
    for (i = 0; i < 8; ++i) {
        int16_t* o = nead_s16(hle, (uint16_t)(dmemo + 2*i));
        *o = nead_clamp_s16(*o + *nead_s16(hle, (uint16_t)(dmemi + 2*i)) + carry[i]);
    }
    alist_add(hle, (uint16_t)(dmemo + 16), (uint16_t)(dmemi + 16),
              (uint16_t)(count - 16));
}

static void HILOGAIN(struct hle_t* hle, uint32_t w1, uint32_t w2)
{
    uint8_t  gain  = (uint8_t)(w1 >> 16); /* unsigned 4.4 */
    uint16_t count = w1 & 0xffff;
    uint16_t dmem  = (w2 >> 16);

    alist_multQ44(hle, dmem, count, gain);
}

static void FILTER(struct hle_t* hle, uint32_t w1, uint32_t w2)
{
    uint8_t  flags   = (w1 >> 16);
    uint32_t address = nead_address(hle, w2);

    if (flags > 1) {
        /* setup: latch the byte count and snapshot the 16-byte
         * coefficient table from DRAM (the ucode DMAs it into DMEM;
         * later DRAM changes must not be observed by the run). */
        hle->alist_nead.filter_count = w1;
        dram_load_u16(hle, (uint16_t*)hle->alist_nead.filter_table, address, 8);
    }
    else {
        uint16_t dmem = w1;

        /* run: flags==1 starts from zeroed state, flags==0 resumes
         * from the 32-byte state at `address`. */
        alist_filter(hle, (flags & 0x1) != 0, dmem,
                     hle->alist_nead.filter_count, address,
                     hle->alist_nead.filter_table,
                     nead_filter_avg != 0, nead_slab);
    }
}

static void S8DEC(struct hle_t* hle, uint32_t w1, uint32_t w2)
{
    if (nead_setbuff_store) alist_nead_s8(hle, w1, w2, nead_setbuff_store, nead_loop_offset, 1);
    else alist_s8dec(hle, (w1 & 0x10000u) != 0, (w1 & 0x20000u) != 0,
        hle->alist_nead.in, hle->alist_nead.out, hle->alist_nead.count, hle->alist_nead.loop, w2 & 0xffffffu);
}
static void S8DEC_SFJ(struct hle_t* hle, uint32_t w1, uint32_t w2)
{
    alist_nead_s8(hle, w1, w2, nead_setbuff_store, nead_loop_offset, 0);
}
static void MULTIPLY(struct hle_t* hle, uint32_t w1, uint32_t w2)
{
    alist_nead_multiply(hle, w1, w2);
}
static void INDEXED(struct hle_t* hle, uint32_t w1, uint32_t w2)
{
    alist_nead_indexed(hle, w1, w2);
}

/* OoT aspMain command 0x03 (AudioSynth_UnkCmd3 in the OoT decomp,
 * issued for bookOffset == 2 samples). w1 low 16 = byte count,
 * w2 = dmemi << 16 | dmemo. */
static void UNKCMD3(struct hle_t* hle, uint32_t w1, uint32_t w2)
{
    uint16_t count = w1;
    uint16_t dmemi = (w2 >> 16);
    uint16_t dmemo = w2;

    nead_v31_load(hle, 0);
    alist_unkcmd3(hle, dmemo, dmemi, count);
}

static void SEGMENT(struct hle_t* hle, uint32_t UNUSED(w1), uint32_t w2)
{
    alist_nead_segment(hle, w2);
}

static void NEAD_16(struct hle_t* hle, uint32_t w1, uint32_t w2)
{
    uint8_t  count      = (w1 >> 16);
    uint16_t dmemi      = w1 + nead_dmem_base;
    uint16_t dmemo      = (w2 >> 16) + nead_dmem_base;
    uint16_t block_size = w2;

    alist_copy_blocks(hle, dmemo, dmemi, block_size, count);
}

static void POLEF(struct hle_t* hle, uint32_t w1, uint32_t w2)
{
    uint8_t  flags   = (w1 >> 16);
    uint16_t gain    = w1;
    uint32_t address = nead_address(hle, w2);

    if (hle->alist_nead.count == 0)
        return;

    alist_polef(
            hle,
            flags & A_INIT,
            hle->alist_nead.out,
            hle->alist_nead.in,
            hle->alist_nead.count,
            gain,
            hle->alist_nead.table,
            address,
            0);
}


void alist_process_nead_mk(struct hle_t* hle)
{
    static const acmd_callback_t ABI[0x20] = {
        SPNOOP,         ADPCM,          CLEARBUFF,      SPNOOP,
        SPNOOP,         RESAMPLE,       SPNOOP,         SEGMENT,
        SETBUFF,        SPNOOP,         DMEMMOVE,       LOADADPCM,
        MIXER,          INTERLEAVE_MK,  POLEF,          SETLOOP,
        NEAD_16,        INTERL,         ENVSETUP1_MK,   ENVMIXER_MK,
        LOADBUFF,       SAVEBUFF,       ENVSETUP2,      SPNOOP,
        SPNOOP,         SPNOOP,         SPNOOP,         SPNOOP,
        SPNOOP,         SPNOOP,         SPNOOP,         SPNOOP
    };

    nead_slab = 0xfa0;
    nead_book = 0x3d0;
    nead_env_style = NEAD_ENV_MK;
    nead_resample_old = 1;
    nead_seed(hle, 0x80);
    nead_dmem_base = 0x450;
    nead_setbuff_store = 0x330;
    nead_loop_offset = 16;
    /* The resident initialization repeatedly clears only table slot zero. */
    *nead_s16(hle, 0x320) = *nead_s16(hle, 0x322) = 0;
    nead_walk(hle, ABI, 0x20, 0x350, 128);
    rsp_break(hle, SP_STATUS_TASKDONE);
}

void alist_process_nead_sf(struct hle_t* hle)
{
    static const acmd_callback_t ABI[0x20] = {
        SPNOOP,         ADPCM,          CLEARBUFF,      SPNOOP,
        ADDMIXER,       RESAMPLE,       RESAMPLE_ZOH,   SPNOOP,
        SETBUFF,        SPNOOP,         DMEMMOVE,       LOADADPCM,
        MIXER,          INTERLEAVE_MK,  POLEF,          SETLOOP,
        NEAD_16,        INTERL,         ENVSETUP1,      ENVMIXER,
        LOADBUFF,       SAVEBUFF,       ENVSETUP2,      SPNOOP,
        HILOGAIN,       MULTIPLY,        DUPLICATE,      SPNOOP,
        SPNOOP,         SPNOOP,         SPNOOP,         SPNOOP
    };

    nead_slab = 0xf90;
    nead_book = 0x3c0;
    nead_env_style = NEAD_ENV_SF;
    nead_resample_old = 0;
    nead_seed(hle, 0x80);
    nead_dmem_base = 0;
    nead_setbuff_store = 0x320;
    nead_loop_offset = 16;
    nead_walk(hle, ABI, 0x20, 0x340, 128);
    rsp_break(hle, SP_STATUS_TASKDONE);
}

void alist_process_nead_sfj(struct hle_t* hle)
{
    static const acmd_callback_t ABI[0x20] = {
        SPNOOP,         ADPCM,          CLEARBUFF,      SPNOOP,
        ADDMIXER,       RESAMPLE,       RESAMPLE_ZOH,   SPNOOP,
        SETBUFF,        SPNOOP,         DMEMMOVE,       LOADADPCM,
        MIXER,          INTERLEAVE_MK,  POLEF,          SETLOOP,
        NEAD_16,        INTERL,         ENVSETUP1,      ENVMIXER,
        LOADBUFF,       SAVEBUFF,       ENVSETUP2,      S8DEC_SFJ,
        HILOGAIN,       MULTIPLY,        DUPLICATE,      SPNOOP,
        SPNOOP,         SPNOOP,         SPNOOP,         SPNOOP
    };

    nead_slab = 0xf90;
    nead_book = 0x3c0;
    nead_env_style = NEAD_ENV_SF;
    nead_resample_old = 0;
    nead_seed(hle, 0x80);
    nead_dmem_base = 0;
    nead_setbuff_store = 0x320;
    nead_loop_offset = 16;
    /* Like the older MK resident, this initialization loop repeatedly
     * stores zero to the same word; it does not advance the address. */
    *nead_s16(hle, 0x320) = *nead_s16(hle, 0x322) = 0;
    nead_walk(hle, ABI, 0x20, 0x340, 128);
    rsp_break(hle, SP_STATUS_TASKDONE);
}

void alist_process_nead_fz(struct hle_t* hle)
{
    static const acmd_callback_t ABI[0x20] = {
        NEAD_00,        ADPCM,          CLEARBUFF,      SPNOOP,
        ADDMIXER,       RESAMPLE,       SPNOOP,         SPNOOP,
        SETBUFF,        SPNOOP,         DMEMMOVE,       LOADADPCM,
        MIXER,          INTERLEAVE,     SPNOOP,         SETLOOP,
        NEAD_16,        INTERL,         ENVSETUP1,      ENVMIXER,
        LOADBUFF,       SAVEBUFF,       ENVSETUP2,      S8DEC,
        SPNOOP,         SPNOOP,        DUPLICATE,      SPNOOP,
        SPNOOP,         SPNOOP,         SPNOOP,         SPNOOP
    };

    nead_slab = 0xfc0;
    nead_book = 0x340;
    nead_env_style = NEAD_ENV_FZ;
    nead_resample_old = 0;
    nead_seed(hle, 0);
    nead_dmem_base = 0;
    nead_setbuff_store = 0x2e0;
    nead_loop_offset = 16;
    nead_walk(hle, ABI, 0x20, 0x300, 64);
    rsp_break(hle, SP_STATUS_TASKDONE);
}

/* These constants are original instruction spans, not fitted residuals:
 * boot/startup CPU 129, text DMA 496, data DMA 102, termination 5;
 * each command has 16 dispatch + 3 return clocks and one list-DMA beat.
 * LOAD/SAVE body CPU is 39/42, SETBUFF is 6. See nead_wave_common.py. */
static void nead_wave_begin(struct hle_t* hle)
{
    uint32_t bytes = *dmem_u32(hle, TASK_DATA_SIZE);
    uint32_t list = *dmem_u32(hle, TASK_DATA_PTR) & 0x7fffffu;
    uint32_t boot = *dmem_u32(hle, TASK_UCODE_BOOT) & 0x7fffffu;
    nead_wave_expected = bytes / 8u;
    nead_wave_observed = 0;
    nead_wave_clocks = 732u + 20u * nead_wave_expected;
    nead_wave_valid = nead_timing_profile == 5u &&
        bytes && bytes <= 128u && !(bytes & 7u) && !(list & 7u) &&
        list <= 0x800000u - bytes &&
        *dmem_u32(hle, TASK_FLAGS) == 0 &&
        *dmem_u32(hle, TASK_UCODE_DATA_SIZE) == 0x330u &&
        *dmem_u32(hle, TASK_UCODE_BOOT_SIZE) == 0xd0u &&
        !(boot & 3u) && boot <= 0x800000u - 0xd0u &&
        !*hle->sp_dma_full && !*hle->sp_dma_busy && !*hle->sp_semaphore &&
        !(*hle->sp_status & 0x80u) && !(*hle->dpc_status & 1u) &&
        hle_timing_word_fingerprint((const uint32_t*)(hle->dram + boot), 52u) ==
            UINT64_C(0x0f7c8dfaf9f62fc0);
}

static void nead_wave_command(struct hle_t* hle, uint32_t opcode, uint32_t clocks)
{
    if (!nead_wave_valid) return;
    uint16_t target = opcode == 0x14u ? 0x1ab4u : opcode == 0x15u ? 0x1ad8u :
                      opcode == 8u ? 0x11f8u : 0x1534u;
    ++nead_wave_observed;
    if (!clocks || nead_wave_observed > nead_wave_expected ||
        nead_setbuff_store != 0x320u || nead_dmem_base ||
        (uint16_t)*nead_s16(hle, (uint16_t)(0x10u + 2u * opcode)) != target)
        nead_wave_valid = 0;
    nead_wave_clocks += clocks;
}

static uint32_t nead_wave_dma(uint32_t w1, uint32_t w2, uint32_t cpu)
{
    uint32_t bytes = (w1 >> 12) & 0xff0u, at = w1 & 0xff8u;
    uint32_t address = w2 & 0xffffffu;
    if (!bytes || (w1 & 0x1000u) || at < 0x400u ||
        at + bytes > 0xf90u || (address & 7u) || address > 0x800000u - bytes)
        return 0;
    return cpu + bytes / 8u;
}

static void WRJB_LOADBUFF(struct hle_t* hle, uint32_t w1, uint32_t w2)
{
    nead_wave_command(hle, 0x14u, nead_wave_dma(w1, w2, 39u));
    LOADBUFF(hle, w1, w2);
}
static void WRJB_SAVEBUFF(struct hle_t* hle, uint32_t w1, uint32_t w2)
{
    uint32_t count = (w1 >> 12) & 0xff0u;
    uint32_t address = w2 & 0xffffffu;
    uint32_t span = count ? count : 4096u * 256u + 4088u * 255u;
    /* SAVEBUFF only reads the SP bank, including wrapped DMEM/IMEM.
     * Decrementing zero encodes 256 rows, each 4096 bytes with 4088 skip. */
    uint32_t clocks = address <= 0x800000u - span ?
        42u + (count ? count / 8u : 131072u) : 0u;
    nead_wave_command(hle, 0x15u, clocks);
    SAVEBUFF(hle, w1, w2);
}
static void WRJB_SETBUFF(struct hle_t* hle, uint32_t w1, uint32_t w2)
{
    nead_wave_command(hle, 8u, 6u);
    SETBUFF(hle, w1, w2);
}
static void WRJB_RESAMPLE(struct hle_t* hle, uint32_t w1, uint32_t w2)
{
    RESAMPLE(hle, w1, w2);
    nead_wave_command(hle, 5u, nead_last_resample_clocks);
}

void alist_process_nead_wrjb(struct hle_t* hle)
{
    static const acmd_callback_t ABI[0x20] = {
        SPNOOP,         ADPCM,          CLEARBUFF,      UNKCMD3,
        ADDMIXER,       WRJB_RESAMPLE,   RESAMPLE_ZOH,   SPNOOP,
        WRJB_SETBUFF,   SPNOOP,          DMEMMOVE,      LOADADPCM,
        MIXER,          INTERLEAVE,     SPNOOP,         SETLOOP,
        NEAD_16,        INTERL,         ENVSETUP1,      ENVMIXER,
        WRJB_LOADBUFF,  WRJB_SAVEBUFF,   ENVSETUP2,     S8DEC_SFJ,
        HILOGAIN,       MULTIPLY,       DUPLICATE,      FILTER,
        SPNOOP,         SPNOOP,         SPNOOP,         SPNOOP
    };

    nead_slab = 0xf90;
    nead_book = 0x3c0;
    nead_filter_avg = 0;
    nead_env_style = NEAD_ENV_SF;
    nead_resample_old = 0;
    nead_seed(hle, 0x80);
    nead_dmem_base = 0;
    nead_setbuff_store = 0x320;
    nead_loop_offset = 16;
    nead_wave_begin(hle);
    nead_walk(hle, ABI, 0x20, 0x340, 128);
    if (nead_wave_valid && nead_wave_observed == nead_wave_expected &&
        nead_wave_clocks <= INT32_MAX)
        hle_timing_override((uint32_t)nead_wave_clocks);
    rsp_break(hle, SP_STATUS_TASKDONE);
}

void alist_process_nead_ys(struct hle_t* hle)
{
    static const acmd_callback_t ABI[0x18] = {
        NEAD_00,        ADPCM,          CLEARBUFF,      UNKCMD3,
        ADDMIXER,       RESAMPLE,       RESAMPLE_ZOH,   FILTER,
        SETBUFF,        DUPLICATE,      DMEMMOVE,       LOADADPCM,
        MIXER,          INTERLEAVE,     HILOGAIN,       SETLOOP,
        NEAD_16,        INTERL,         ENVSETUP1,      ENVMIXER,
        LOADBUFF,       SAVEBUFF,       ENVSETUP2,      S8DEC
    };

    nead_slab = 0xfc0;
    nead_book = 0x340;
    nead_filter_avg = 0;
    nead_env_style = NEAD_ENV_SF;
    nead_resample_old = 0;
    nead_seed(hle, 0);
    nead_dmem_base = 0;
    nead_setbuff_store = 0x2e0;
    nead_loop_offset = 16;
    nead_walk(hle, ABI, 0x18, 0x300, 64);
    rsp_break(hle, SP_STATUS_TASKDONE);
}

void alist_process_nead_1080(struct hle_t* hle)
{
    static const acmd_callback_t ABI[0x18] = {
        NEAD_00,        ADPCM,          CLEARBUFF,      UNKCMD3,
        ADDMIXER,       RESAMPLE,       RESAMPLE_ZOH,   FILTER,
        SETBUFF,        DUPLICATE,      DMEMMOVE,       LOADADPCM,
        MIXER,          INTERLEAVE,     HILOGAIN,       SETLOOP,
        NEAD_16,        INTERL,         ENVSETUP1,      ENVMIXER,
        LOADBUFF,       SAVEBUFF,       ENVSETUP2,      S8DEC
    };

    nead_slab = 0xfc0;
    nead_book = 0x340;
    nead_filter_avg = 0;
    nead_env_style = NEAD_ENV_OOT;
    nead_resample_old = 0;
    nead_seed(hle, 0);
    nead_dmem_base = 0;
    nead_setbuff_store = 0x2e0;
    nead_loop_offset = 16;
    nead_walk(hle, ABI, 0x18, 0x300, 64);
    rsp_break(hle, SP_STATUS_TASKDONE);
}

void alist_process_nead_oot(struct hle_t* hle)
{
    static const acmd_callback_t ABI[0x18] = {
        NEAD_00,        ADPCM,          CLEARBUFF,      UNKCMD3,
        ADDMIXER,       RESAMPLE,       RESAMPLE_ZOH,   FILTER,
        SETBUFF,        DUPLICATE,      DMEMMOVE,       LOADADPCM,
        MIXER,          INTERLEAVE,     HILOGAIN,       SETLOOP,
        NEAD_16,        INTERL,         ENVSETUP1,      ENVMIXER,
        LOADBUFF,       SAVEBUFF,       ENVSETUP2,      S8DEC
    };

    nead_slab = 0xfb0;
    nead_book = 0x330;
    nead_filter_avg = 1;
    nead_env_style = NEAD_ENV_OOT;
    nead_resample_old = 0;
    nead_seed(hle, 0);
    nead_dmem_base = 0;
    nead_setbuff_store = 0x2e0;
    nead_loop_offset = 8;
    nead_walk(hle, ABI, 0x18, 0x2f0, 64);
    rsp_break(hle, SP_STATUS_TASKDONE);
}

void alist_process_nead_mm(struct hle_t* hle)
{
    static const acmd_callback_t ABI[0x18] = {
        MULTIPLY,       ADPCM,          CLEARBUFF,      SPNOOP,
        ADDMIXER,       RESAMPLE,       RESAMPLE_ZOH,   FILTER,
        SETBUFF,        DUPLICATE,      DMEMMOVE,       LOADADPCM,
        MIXER,          INTERLEAVE,     HILOGAIN,       SETLOOP,
        NEAD_16,        INTERL,         ENVSETUP1,      ENVMIXER,
        LOADBUFF,       SAVEBUFF,       ENVSETUP2,      S8DEC
    };

    nead_slab = 0xfb0;
    nead_book = 0x330;
    nead_filter_avg = 1;
    nead_env_style = NEAD_ENV_OOT;
    nead_resample_old = 0;
    nead_seed(hle, 0);
    /* The resident stores TASK_DRAM_STACK at t8+12, with t8=0x2e0. */
    *nead_s16(hle, 0x2ec) = (int16_t)(*dmem_u32(hle, TASK_DRAM_STACK) >> 16);
    *nead_s16(hle, 0x2ee) = (int16_t)*dmem_u32(hle, TASK_DRAM_STACK);
    nead_dmem_base = 0;
    nead_setbuff_store = 0x2e0;
    nead_loop_offset = 8;
    nead_walk(hle, ABI, 0x18, 0x2f0, 64);
    rsp_break(hle, SP_STATUS_TASKDONE);
}

void alist_process_nead_mmb(struct hle_t* hle)
{
    static const acmd_callback_t ABI[0x18] = {
        SPNOOP,         ADPCM,          CLEARBUFF,      SPNOOP,
        ADDMIXER,       RESAMPLE,       RESAMPLE_ZOH,   FILTER,
        SETBUFF,        DUPLICATE,      DMEMMOVE,       LOADADPCM,
        MIXER,          INTERLEAVE,     HILOGAIN,       SETLOOP,
        NEAD_16,        INTERL,         ENVSETUP1,      ENVMIXER,
        LOADBUFF,       SAVEBUFF,       ENVSETUP2,      S8DEC
    };

    nead_slab = 0xfb0;
    nead_book = 0x330;
    nead_filter_avg = 1;
    nead_env_style = NEAD_ENV_OOT;
    nead_resample_old = 0;
    nead_seed(hle, 0);
    *nead_s16(hle, 0x2ec) = (int16_t)(*dmem_u32(hle, TASK_DRAM_STACK) >> 16);
    *nead_s16(hle, 0x2ee) = (int16_t)*dmem_u32(hle, TASK_DRAM_STACK);
    nead_dmem_base = 0;
    nead_setbuff_store = 0x2e0;
    nead_loop_offset = 8;
    nead_walk(hle, ABI, 0x18, 0x2f0, 64);
    rsp_break(hle, SP_STATUS_TASKDONE);
}

void alist_process_nead_ac(struct hle_t* hle)
{
    static const acmd_callback_t ABI[0x18] = {
        INDEXED,        ADPCM,          CLEARBUFF,      SPNOOP,
        ADDMIXER,       RESAMPLE,       RESAMPLE_ZOH,   FILTER,
        SETBUFF,        DUPLICATE,      DMEMMOVE,       LOADADPCM,
        MIXER,          INTERLEAVE,     HILOGAIN,       SETLOOP,
        NEAD_16,        INTERL,         ENVSETUP1,      ENVMIXER,
        LOADBUFF,       SAVEBUFF,       ENVSETUP2,      S8DEC
    };

    nead_slab = 0xfb0;
    nead_book = 0x300;
    nead_filter_avg = 1;
    nead_env_style = NEAD_ENV_OOT;
    nead_resample_old = 0;
    nead_seed(hle, 0);
    *nead_s16(hle, 0x2ec) = (int16_t)(*dmem_u32(hle, TASK_DRAM_STACK) >> 16);
    *nead_s16(hle, 0x2ee) = (int16_t)*dmem_u32(hle, TASK_DRAM_STACK);
    nead_dmem_base = 0;
    nead_setbuff_store = 0x2e0;
    nead_loop_offset = 8;
    nead_walk(hle, ABI, 0x18, 0x2f0, 8);
    rsp_break(hle, SP_STATUS_TASKDONE);
}

void alist_process_nead_mats(struct hle_t* hle)
{
    /* Talent Studio's disk program uses 00 for signed multiplication;
     * 03 points directly to the command-loop return at 094. */
    static const acmd_callback_t ABI[0x18] = {
        MULTIPLY,       ADPCM,          CLEARBUFF,      SPNOOP,
        ADDMIXER,       RESAMPLE,       RESAMPLE_ZOH,   FILTER,
        SETBUFF,        DUPLICATE,      DMEMMOVE,       LOADADPCM,
        MIXER,          INTERLEAVE,     HILOGAIN,       SETLOOP,
        NEAD_16,        INTERL,         ENVSETUP1,      ENVMIXER,
        LOADBUFF,       SAVEBUFF,       ENVSETUP2,      S8DEC
    };

    nead_slab = 0xfb0;
    nead_book = 0x330;
    nead_filter_avg = 1;
    nead_env_style = NEAD_ENV_OOT;
    nead_resample_old = 0;
    nead_seed(hle, 0);
    *nead_s16(hle, 0x2ec) = (int16_t)(*dmem_u32(hle, TASK_DRAM_STACK) >> 16);
    *nead_s16(hle, 0x2ee) = (int16_t)*dmem_u32(hle, TASK_DRAM_STACK);
    nead_dmem_base = 0;
    nead_setbuff_store = 0x2e0;
    nead_loop_offset = 8;
    nead_walk(hle, ABI, 0x18, 0x2f0, 64);
    rsp_break(hle, SP_STATUS_TASKDONE);
}

void alist_process_nead_efz(struct hle_t* hle)
{
    /* F-Zero X: Expansion Kit (64DD), ucode signature 0x1f4c1230.
     * The Expansion Kit reuses the F-Zero X cartridge audio engine and shares
     * its nead_fz command layout, so dispatch through the same processor. */
    alist_process_nead_fz(hle);
}
