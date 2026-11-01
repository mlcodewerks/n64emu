#pragma once
#include <stdbool.h>
#include <libretro.h>
#include "device/rcp/rdp/glrdp/rdp_accuracy.h"
#ifdef __cplusplus
extern "C" {
#endif
enum renderer_type { RENDERER_SOFTRDP, RENDERER_PARALLEL, RENDERER_GLRDP, RENDERER_DX11, RENDERER_GL2, RENDERER_D3D9, RENDERER_GLES2 };
static inline bool renderer_legacy_gpu(enum renderer_type type) {
    return type == RENDERER_GL2 || type == RENDERER_D3D9 || type == RENDERER_GLES2;
}
static inline bool renderer_native_gpu(enum renderer_type type) {
    return type == RENDERER_GLRDP || type == RENDERER_DX11;
}
enum hle_level { HLE_OFF, HLE_RSP };
enum controller_pak_option { CONTROLLER_PAK_NONE, CONTROLLER_PAK_MEMORY,
    CONTROLLER_PAK_RUMBLE, CONTROLLER_PAK_TRANSFER };
struct renderer_settings {
    /* RSP execution is independent of CPU cache timing. */
    bool rsp_hle, lle_graphics, lle_audio;
    enum hle_level hle;
    /* Derived from Off / Fast / Accurate. Component clocks
     * remain active; CPU timing selects none, tags, or full cache contents. */
    bool per_cycle_timing;
    bool component_timing;
    bool fast_cpu_timing;
    enum renderer_type renderer;
    struct GLRDPConfig glrdp;
    unsigned threads, upscale, downscale;
    bool performance_only, vi_filter, dedither, blur, overscan, bob;
    bool native_lod, native_tex_rect, ss_readbacks, ss_dither;
};
extern struct renderer_settings renderer_settings;
/* Full timing always includes component clocks. Fast CPU execution can retain
 * work-derived RSP/RDP deadlines without emulating CPU cache contents. */
static inline bool renderer_component_timing(void)
{
    return renderer_settings.per_cycle_timing || renderer_settings.component_timing;
}
void renderer_register_options(retro_environment_t environment);
void renderer_read_options(retro_environment_t environment);
void renderer_read_live_options(retro_environment_t environment);
enum controller_pak_option controller_read_pak_option(retro_environment_t environment, unsigned port);
#ifdef __cplusplus
}
#endif
