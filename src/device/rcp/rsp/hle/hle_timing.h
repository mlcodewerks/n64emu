/* Work-based RSP clock estimates. Counters describe executed work, never
 * host wall time; resets happen once per HLE invocation. */
#ifndef HLE_TIMING_H
#define HLE_TIMING_H
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
enum hle_work_kind {
    HLE_WORK_COMMAND, HLE_WORK_DMA, HLE_WORK_COPY, HLE_WORK_ADPCM,
    HLE_WORK_RESAMPLE, HLE_WORK_ENVMIX, HLE_WORK_MIX, HLE_WORK_FILTER,
    HLE_WORK_VERTEX, HLE_WORK_TRIANGLE, HLE_WORK_MATRIX, HLE_WORK_VOICE,
    HLE_WORK_EFFECT, HLE_WORK_BLOCK, HLE_WORK_PIXEL, HLE_WORK_MP3,
    HLE_WORK_RDP, HLE_WORK_LIGHT, HLE_WORK_MATRIX_MUL, HLE_WORK_DMA_SETUP,
    HLE_WORK_INDEX,
    HLE_WORK_COUNT
};
/* Executed paths, separate from the legacy work/model ABI. Keep the names
 * alongside the enum so offline replay can export an unambiguous schema. */
#define HLE_TIMING_PATHS(X) \
    X(TRI_INPUT) X(TRI_REJECT) X(TRI_DIRECT) X(TRI_CLIP) \
    X(CLIP_PLANE) X(CLIP_DONE) X(CLIP_LIST_SWAP) X(CLIP_INDEX_RESET) \
    X(CLIP_COPY) X(CLIP_FETCH) X(CLIP_VERTEX) X(CLIP_TRANSITION) \
    X(CLIP_SENTINEL) X(CLIP_WRAP) X(CLIP_KEEP) X(CLIP_OUTSIDE) \
    X(CLIP_START) X(CLIP_RESTART) X(CLIP_ENTER) X(CLIP_EXIT) \
    X(CLIP_NEG_DENOM) X(CLIP_FAN) X(CLIP_SHADOW) X(CLIP_UNMAPPED) \
    X(TRI_CORE) X(SORT_PASS) X(SORT_SWAP_01) X(SORT_SWAP_12) \
    X(SORT_NEGATE) X(SORT_TABLE) X(SORT_NETWORK) \
    X(CULL_FIRST) X(CULL_SECOND) X(TRI_DEGENERATE) \
    X(TRI_TEXTURE) X(TRI_SHADE) X(TRI_DEPTH) X(TRI_FLAT) \
    X(TRI_DEPTH_CLAMP) X(VTX_FOG)
enum hle_path_kind {
#define HLE_PATH_ENUM(name) HLE_PATH_##name,
    HLE_TIMING_PATHS(HLE_PATH_ENUM)
#undef HLE_PATH_ENUM
    HLE_PATH_COUNT
};
struct hle_timing_work {
    uint32_t units[HLE_WORK_COUNT];
    /* Verified source regions replace their own work-unit cost. Keep the
     * observed work vector intact for replay/calibration and saved walkers. */
    uint32_t refined_units[HLE_WORK_COUNT];
    uint32_t refined_cycles[HLE_WORK_COUNT];
    uint32_t refined_mask;
    /* Preserve command identity: setup/no-op/branch commands do not have
     * interchangeable dispatch and fixed handler costs. */
    uint32_t commands[256];
    uint32_t paths[HLE_PATH_COUNT];
    uint32_t paths_saturated;
    uint32_t override_cycles;
    uint32_t ucode_checksum;
    const char *family;
    /* Invocation-local instruction profiles; not part of a serialized
     * audio/graphics snapshot or the legacy 21-counter replay ABI. */
    uint32_t sdk_boot, native_graphics_profile, native_opcode, native_graphics_overlay;
    int32_t native_packet_cost;
};
void hle_timing_begin(void);
void hle_timing_boot(const uint32_t *imem);
void hle_timing_native_graphics(uint32_t profile);
uint32_t hle_timing_native_graphics_profile(void);
int hle_timing_graphics_bg_enter(void);
uint32_t hle_timing_native_opcode(void);
void hle_timing_graphics_command(uint32_t w0);
void hle_timing_graphics_refine_command(uint32_t cycles,int32_t packet_cost);
void hle_timing_graphics_rdp(uint32_t units);
void hle_timing_graphics_flush(uint32_t bytes,int wrapped);
void hle_timing_ucode(uint32_t checksum);
void hle_timing_family(const char *family);
void hle_timing_graphics_family(const char *family);
void hle_timing_add(enum hle_work_kind kind, uint32_t units);
int hle_timing_refine(enum hle_work_kind kind, uint32_t units, uint32_t cycles);
void hle_timing_command(unsigned int opcode);
void hle_timing_path(enum hle_path_kind kind, uint32_t count);
const char *hle_timing_path_name(unsigned int kind);
/* Rare/Fast3D's native pointer-list walk, fed the HLE polygon's stored flags.
 * This counts native restarts/sentinels, rather than the shorter C clip loop. */
void hle_timing_clip_walk(const uint32_t *flags, unsigned int count, uint32_t mask);
void hle_timing_sort3(int32_t y0, int32_t y1, int32_t y2);
/* Exact covered region only, not a second charge on top of the work model.
 * Returns zero for an unverified microcode/route. */
int hle_timing_sort_cycles(uint64_t *cycles);
void hle_timing_override(uint32_t cycles);
uint32_t hle_timing_cycles(void);
void hle_timing_set_slice_limit(uint32_t cycles);
uint32_t hle_timing_slice_limit(void);
struct hle_timing_work hle_timing_get(void);
void hle_timing_restore(struct hle_timing_work saved);
#ifdef __cplusplus
}
#endif
#endif
