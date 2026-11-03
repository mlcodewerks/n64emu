#include "hle_timing.h"
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
    model_valid = 0;
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

static const struct timing_model *selected;

static void select_model(void)
{
    unsigned int i;
    selected = &fallback;
    for (i = 0; i < sizeof(timing_models) / sizeof(timing_models[0]); i++)
        if (work.family && !strcmp(work.family, timing_models[i].family) &&
            (timing_models[i].checksum == 0xffffffffu || work.ucode_checksum == timing_models[i].checksum))
        { selected = &timing_models[i]; break; }
    model_valid = 1;
}

uint32_t hle_timing_cycles(void)
{
    const struct timing_model *model;
    uint64_t cycles;
    unsigned int i;
    if (work.override_cycles) return work.override_cycles;
    if (!model_valid) select_model();
    model = selected;
    cycles = model->base;
    for (i = 0; i < HLE_WORK_COUNT; i++) {
        uint64_t cost = (uint64_t)model->cost[i] * work.units[i];
        if (cycles >= INT_MAX || cost > INT_MAX - cycles) return INT_MAX;
        cycles += cost;
    }
    return cycles ? (uint32_t)cycles : 1;
}
