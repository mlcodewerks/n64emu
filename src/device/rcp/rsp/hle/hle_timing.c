#include "hle_timing.h"
#include "hle_timing_s2dex.h"
#include <string.h>
#include <limits.h>

static struct hle_timing_work work;
static int model_valid;
static uint32_t slice_limit;
void hle_timing_set_slice_limit(uint32_t cycles){slice_limit=cycles;}
uint32_t hle_timing_slice_limit(void){return slice_limit;}

void hle_timing_begin(void)
{
    memset(&work, 0, sizeof(work));
    work.family = "unknown";
    work.native_packet_cost = -1;
    model_valid = 0;
}
void hle_timing_boot(const uint32_t *imem)
{
    work.sdk_boot = imem && hle_timing_word_fingerprint(imem,0xd0u/4u)
        == UINT64_C(0x0f7c8dfaf9f62fc0);
}
void hle_timing_native_graphics(uint32_t profile)
{
    work.native_graphics_profile = work.sdk_boot ? profile : 0;
    work.native_graphics_overlay = 0;
    work.native_packet_cost = -1;
}
uint32_t hle_timing_native_graphics_profile(void) { return work.native_graphics_profile; }
int hle_timing_graphics_bg_enter(void)
{
    int cold;
    if(work.native_graphics_profile!=1)return -1;
    cold=work.native_graphics_overlay==2 ? -1 : work.native_graphics_overlay==0;
    work.native_graphics_overlay=1;
    hle_timing_add(HLE_WORK_DMA,5u+(cold==1?230u:0u));
    return cold;
}
uint32_t hle_timing_native_opcode(void) { return work.native_opcode; }
void hle_timing_graphics_refine_command(uint32_t cycles,int32_t packet_cost)
{
    if(work.native_graphics_profile==1 && cycles &&
       hle_timing_refine(HLE_WORK_COMMAND,1,cycles)) work.native_packet_cost=packet_cost;
}
/* Original 2.06 550..63c plus its 12-clock resident dispatch.
 * Includes two segment resolutions, the 24-byte read and its idle DMA
 * polling; 644's no-drain return is included. 598's resident branch skips
 * the entire writer. 5ac and 604 each add one clock on their emit path.
 * The separate flush callback adds the excess over the no-drain return. */
void hle_timing_s2dex206_load(int resident,int history)
{
    uint32_t cycles = resident ? 72u : 125u + (history == -127) + (history >= 0);
    if(hle_timing_refine(HLE_WORK_COMMAND,1,cycles))
        work.native_packet_cost=HLE_PACKET_S2DEX206_LOAD;
}
void hle_timing_graphics_command(uint32_t w0)
{
    int32_t packet_cost;
    uint32_t cycles;
    hle_timing_command(w0>>24);
    work.native_opcode=w0>>24;
    work.native_packet_cost=-1;
    if(work.native_graphics_profile!=1)return;
    cycles=hle_timing_s2dex_command(w0,&packet_cost);
    /* The sprite/select trampolines restore the resident main overlay.
     * Uncovered handlers may change the overlay; don't assume a later
     * background is either cold or warm without that evidence. */
    if((w0>>24)==3 || (w0>>24)==4)work.native_graphics_overlay=0;
    else if(!cycles && (w0>>24)!=0 && (w0>>24)!=1 && (w0>>24)!=2 &&
            (w0>>24)!=5 && (w0>>24)!=6 && (w0>>24)!=0xb8 &&
            (w0>>24)!=0xe4 && (w0>>24)!=0xe5 &&
            (w0>>24)!=0xb3 && (w0>>24)!=0xb4)
        work.native_graphics_overlay=2;
    hle_timing_graphics_refine_command(cycles,packet_cost);
}
void hle_timing_graphics_rdp(uint32_t units)
{
    hle_timing_add(HLE_WORK_RDP,units);
    if(work.native_packet_cost==HLE_PACKET_S2DEX206_LOAD)
        hle_timing_refine(HLE_WORK_RDP,units,0);
    else if(work.native_graphics_profile==1 && work.native_packet_cost>=0) {
        uint64_t clocks=(uint64_t)units*(uint32_t)work.native_packet_cost;
        hle_timing_refine(HLE_WORK_RDP,units,clocks>INT_MAX?INT_MAX:(uint32_t)clocks);
    }
}
void hle_timing_graphics_flush(uint32_t bytes,int wrapped)
{
    uint32_t extra;
    if(work.native_packet_cost==HLE_PACKET_S2DEX206_LOAD) {
        uint32_t beats=(bytes+7u)/8u;
        /* Original 644..6bc and the shared fd8 writer: 34 extra idle
         * clocks beyond the included no-drain path; 66c's wrap adds ten. */
        hle_timing_add(HLE_WORK_DMA,beats);
        hle_timing_refine(HLE_WORK_DMA,beats,beats);
        hle_timing_add(HLE_WORK_DMA_SETUP,1);
        hle_timing_refine(HLE_WORK_DMA_SETUP,1,34u+(wrapped?10u:0u));
        return;
    }
    if(work.native_graphics_profile!=1)return;
    hle_timing_add(HLE_WORK_DMA,(bytes+7u)/8u);
    /* 0xde8..return: 46 + bytes/8, plus ten clocks on ring wrap.
     * BG's normal call already covers the seven-clock no-flush return;
     * ordinary commands cover the four-clock 0xddc fast branch instead. */
    extra=work.native_opcode==1 ? 39u : work.native_packet_cost>=0 ? 45u : 0;
    if(extra) {
        extra+=wrapped?10u:0u;
        hle_timing_add(HLE_WORK_DMA_SETUP,1);
        hle_timing_refine(HLE_WORK_DMA_SETUP,1,extra);
    }
}
void hle_timing_family(const char *family)
{
    if (work.family != family) model_valid = 0;
    work.family = family;
}
void hle_timing_ucode(uint32_t checksum)
{
    if (work.ucode_checksum != checksum) model_valid = 0;
    work.ucode_checksum = checksum;
}
void hle_timing_graphics_family(const char *family)
{
    if (work.family && (!strcmp(work.family, "send_dlist_to_gfx_plugin") ||
                        !strcmp(work.family, "streaming_gfx_task")))
        hle_timing_family(family);
}
void hle_timing_add(enum hle_work_kind kind, uint32_t units)
{
    uint32_t old;
    if ((unsigned int)kind >= HLE_WORK_COUNT) return;
    old = work.units[kind];
    work.units[kind] = units > UINT_MAX - old ? UINT_MAX : old + units;
}
void hle_timing_command(unsigned int opcode)
{
    hle_timing_add(HLE_WORK_COMMAND, 1);
    if (opcode < 256 && work.commands[opcode] != UINT_MAX)
        ++work.commands[opcode];
}
int hle_timing_refine(enum hle_work_kind kind, uint32_t units, uint32_t cycles)
{
    uint32_t old;
    if ((unsigned int)kind >= HLE_WORK_COUNT || !units || work.units[kind] == UINT_MAX ||
        work.refined_units[kind] > work.units[kind] ||
        units > work.units[kind] - work.refined_units[kind]) return 0;
    work.refined_units[kind] += units;
    work.refined_mask |= 1u << (unsigned int)kind;
    old = work.refined_cycles[kind];
    work.refined_cycles[kind] = old >= INT_MAX || cycles > (uint32_t)INT_MAX - old
        ? INT_MAX : old + cycles;
    return 1;
}
void hle_timing_path(enum hle_path_kind kind, uint32_t count)
{
    uint32_t old;
    if ((unsigned int)kind >= HLE_PATH_COUNT) return;
    old = work.paths[kind];
    if (count > UINT_MAX - old) work.paths_saturated = 1;
    work.paths[kind] = count > UINT_MAX - old ? UINT_MAX : old + count;
}
const char *hle_timing_path_name(unsigned int kind)
{
    static const char *const names[] = {
#define HLE_PATH_NAME(name) #name,
        HLE_TIMING_PATHS(HLE_PATH_NAME)
#undef HLE_PATH_NAME
    };
    return kind < HLE_PATH_COUNT ? names[kind] : 0;
}

void hle_timing_clip_walk(const uint32_t *flags, unsigned int count, uint32_t mask)
{
    unsigned int index = 0, transition = 0, steps = 0;
    uint32_t state = 0;
    hle_timing_path(HLE_PATH_CLIP_SHADOW, 1);
    hle_timing_path(HLE_PATH_CLIP_PLANE, 1);
    hle_timing_path(HLE_PATH_CLIP_INDEX_RESET, 1);
copy:
    hle_timing_path(HLE_PATH_CLIP_COPY, 1);
fetch:
    /* A convex polygon has at most two crossings. Bound malformed inputs;
     * report incomplete coverage rather than inventing a timing charge. */
    if (++steps > 8 * (count + 1) || !count || count > 64) {
        hle_timing_path(HLE_PATH_CLIP_UNMAPPED, 1);
        return;
    }
    hle_timing_path(HLE_PATH_CLIP_FETCH, 1);
    if (index == count) {
        hle_timing_path(HLE_PATH_CLIP_SENTINEL, 1);
        if (transition > 2) {
            hle_timing_path(HLE_PATH_CLIP_WRAP, 1);
            index = 0;
            goto fetch;
        }
        hle_timing_path(transition == 2 ? HLE_PATH_CLIP_KEEP : HLE_PATH_CLIP_OUTSIDE, 1);
        return;
    }
    hle_timing_path(HLE_PATH_CLIP_VERTEX, 1);
    if ((flags[index++] & mask) != state) goto copy;
    hle_timing_path(HLE_PATH_CLIP_TRANSITION, 1);
    state ^= mask;
    transition += 2;
    switch (transition) {
    case 2:
        hle_timing_path(HLE_PATH_CLIP_START, 1);
        hle_timing_path(HLE_PATH_CLIP_INDEX_RESET, 1);
        index = 0;
        goto copy;
    case 4:
        hle_timing_path(HLE_PATH_CLIP_RESTART, 1);
        goto copy;
    case 6:
        hle_timing_path(HLE_PATH_CLIP_ENTER, 1);
        goto copy;
    case 8:
        hle_timing_path(HLE_PATH_CLIP_EXIT, 1);
        hle_timing_path(HLE_PATH_CLIP_LIST_SWAP, 1);
        return;
    default:
        hle_timing_path(HLE_PATH_CLIP_UNMAPPED, 1);
    }
}
void hle_timing_override(uint32_t cycles)
{
    work.override_cycles = cycles > 0x7fffffffu ? 0x7fffffffu : cycles;
}
void hle_timing_sort3(int32_t y0, int32_t y1, int32_t y2)
{
    int32_t tmp;
    unsigned int parity = 0;
    for (;;) {
        hle_timing_path(HLE_PATH_SORT_PASS, 1);
        if (y1 < y0) {
            hle_timing_path(HLE_PATH_SORT_SWAP_01, 1);
            tmp = y0; y0 = y1; y1 = tmp;
            parity ^= 1;
        }
        if (y2 >= y1) break;
        hle_timing_path(HLE_PATH_SORT_SWAP_12, 1);
        tmp = y1; y1 = y2; y2 = tmp;
        parity ^= 1;
    }
    if (parity) hle_timing_path(HLE_PATH_SORT_NEGATE, 1);
}
int hle_timing_sort_cycles(uint64_t *cycles)
{
    if (!cycles) return 0;
    *cycles = 0;
    if (!work.family || strcmp(work.family, "gfx_f3d") ||
        (work.ucode_checksum != 0x2212cu && work.ucode_checksum != 0x21e12u) ||
        work.paths[HLE_PATH_CLIP_UNMAPPED] || work.paths_saturated) return 0;
    /* GoldenEye 0xa48..0xa9c; Blast Corps 0xa50..0xaa4, inclusive.
     * One no-swap pass: 8 clocks including both taken redirects and
     * scalar/vector pairs. First swap: 7 scalar instructions replace a
     * redirect (+6). Second swap: 8 clocks replace a redirect (+7).
     * The post-sort cross negation and its boundary interlocks are OUTSIDE
     * this region. See check_paths.py for per-PC/opcode verification. */
    *cycles = UINT64_C(8) * work.paths[HLE_PATH_SORT_PASS]
            + UINT64_C(6) * work.paths[HLE_PATH_SORT_SWAP_01]
            + UINT64_C(7) * work.paths[HLE_PATH_SORT_SWAP_12];
    return 1;
}
struct hle_timing_work hle_timing_get(void) { return work; }
void hle_timing_restore(struct hle_timing_work saved) { work = saved; model_valid = 0; }

/* Units: commands, 8-byte DMA beats, 16-byte copies, 16-sample ADPCM
 * frames, 8-sample resample/env/mix/filter groups, vertices, triangles,
 * matrices, voices, effect taps, image blocks, 8 pixels, MP3 granules.
 * The default follows the instruction loops; measured family-specific
 * coefficients are generated by benchmarks/hle_timing_fit.py. */
struct timing_model { const char *family; uint32_t checksum, base; uint32_t cost[HLE_WORK_COUNT]; };
static const struct timing_model fallback = {
    "default", 0, 80, { 12, 1, 4, 90, 40, 56, 8, 48, 72, 180, 110,
                      1500, 500, 1200, 32, 2200, 6, 24, 110, 16, 4 }
};
#include "hle_timing_models.h"

struct timing_path_model {
    const char *family;
    uint32_t checksum, base_q8;
    uint32_t work_q8[HLE_WORK_COUNT], path_q8[8];
};
#include "hle_timing_path_models.h"
static const struct timing_path_model *selected_path;
static const struct timing_model *selected;

static void select_model(void)
{
    unsigned int i;
    selected = &fallback;
    for (i = 0; i < sizeof(timing_models) / sizeof(timing_models[0]); i++)
        if (work.family && !strcmp(work.family, timing_models[i].family) &&
            (timing_models[i].checksum == 0xffffffffu || work.ucode_checksum == timing_models[i].checksum))
        { selected = &timing_models[i]; break; }
    selected_path = NULL;
    for (i = 0; i < sizeof(timing_path_models) / sizeof(timing_path_models[0]); ++i)
        if (work.family && !strcmp(work.family, timing_path_models[i].family) &&
            work.ucode_checksum == timing_path_models[i].checksum) {
            selected_path = &timing_path_models[i]; break;
        }
    model_valid = 1;
}

static uint32_t coarse_timing_cycles(void)
{
    const struct timing_model *model;
    uint64_t cycles;
    unsigned int i;
    if (work.override_cycles) return work.override_cycles;
    if (!model_valid) select_model();
    model = selected;
    cycles = model->base;
    if (!work.refined_mask) {
        /* Most tasks still use only the coarse work model. Avoid probing
         * two coverage arrays for each kind on that common path. */
        for (i = 0; i < HLE_WORK_COUNT; i++) {
            uint64_t cost = (uint64_t)model->cost[i] * work.units[i];
            if (cycles >= INT_MAX || cost > INT_MAX - cycles) return INT_MAX;
            cycles += cost;
        }
        return cycles ? (uint32_t)cycles : 1;
    }
    for (i = 0; i < HLE_WORK_COUNT; i++) {
        uint32_t units = work.units[i];
        uint64_t cost;
        if (work.refined_units[i] <= units) {
            units -= work.refined_units[i];
            cost = work.refined_cycles[i];
        } else cost = 0;
        cost += (uint64_t)model->cost[i] * units;
        if (cycles >= INT_MAX || cost > INT_MAX - cycles) return INT_MAX;
        cycles += cost;
    }
    return cycles ? (uint32_t)cycles : 1;
}


/* Spatial work profiles use Q8 coefficients so short tasks do not accumulate
 * a rounding error on every vertex or packet. Existing models retain their
 * original integer arithmetic and checksum selection. */
uint32_t hle_timing_cycles(void)
{
    uint64_t cycles, limit = (uint64_t)INT_MAX * 256u;
    unsigned int i;
    if (work.override_cycles) return work.override_cycles;
    if (!model_valid) select_model();
    if (!selected_path) return coarse_timing_cycles();
    cycles = selected_path->base_q8;
    for (i = 0; i < HLE_WORK_COUNT; ++i) {
        uint32_t units = work.units[i];
        uint64_t cost = 0;
        if (work.refined_mask && work.refined_units[i] <= units) {
            units -= work.refined_units[i];
            cost = (uint64_t)work.refined_cycles[i] * 256u;
        }
        cost += (uint64_t)selected_path->work_q8[i] * units;
        if (cycles >= limit || cost > limit - cycles) return INT_MAX;
        cycles += cost;
    }
    for (i = 0; i < 8; ++i) {
        uint64_t cost = (uint64_t)selected_path->path_q8[i] *
            work.paths[timing_path_kinds[i]];
        if (cycles >= limit || cost > limit - cycles) return INT_MAX;
        cycles += cost;
    }
    cycles >>= 8;
    if (!cycles) cycles = 1;
    /* A partial clip shadow must not lower the published producer time.
     * Retaining the maximum also preserves prefix monotonicity when the
     * incomplete/saturated flag first appears. */
    if (work.paths_saturated || work.paths[HLE_PATH_CLIP_UNMAPPED]) {
        uint32_t coarse = coarse_timing_cycles();
        if (cycles < coarse) cycles = coarse;
    }
    return (uint32_t)cycles;
}

/* Own FIFO 2.08 resident MOVEWORD: 038..048 is eight issue/interlock/
 * redirect clocks; resident dispatch adds twelve. The wait through fc8
 * when 194 is a JAL delay slot belongs to the preceding DMA operation.
 * Command-buffer DMA and task startup are charged separately. */
void hle_timing_gbi208_moveword(void)
{
    if(work.sdk_boot)hle_timing_refine(HLE_WORK_COMMAND,1,20u);
}

uint32_t hle_timing_path_count(enum hle_path_kind kind)
{return (unsigned int)kind<HLE_PATH_COUNT?work.paths[kind]:0;}
void hle_timing_gbi208_rejected(unsigned int triangles)
{
    /* Both original kernels take 24 clocks through the caller return.
     * TRI1 adds 12 dispatch + 2 entry; TRI2/QUAD adds 12 + 5 entry.
     * No packet or draw work is charged on this complete reject route. */
    if(work.sdk_boot && (triangles==1u || triangles==2u))
        hle_timing_refine(HLE_WORK_COMMAND,1,triangles==1u?38u:65u);
}
