#pragma once
#include <stdbool.h>
#include <libretro.h>
#ifdef __cplusplus
extern "C" {
#endif

enum controller_pak_option { CONTROLLER_PAK_NONE, CONTROLLER_PAK_MEMORY,
    CONTROLLER_PAK_RUMBLE, CONTROLLER_PAK_TRANSFER };
#if !defined(N64EMU_USF)
#define N64EMU_RELAXED_FP_STATUS_AVAILABLE 1
#else
#define N64EMU_RELAXED_FP_STATUS_AVAILABLE 0
#endif
struct renderer_settings {
    /* Games retain caches, hazards and component timing. Off may omit disabled
     * FP inexact status updates; USF keeps its separate ripper clock. */
    bool per_cycle_timing;
    bool component_timing;
    bool relaxed_fp_status;
    unsigned threads, upscale;
    bool vi_filter, dedither, blur, overscan, bob;
    bool native_lod;
};
extern struct renderer_settings renderer_settings;
/* Component deadlines follow the selected game/USF timing policy. */
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
