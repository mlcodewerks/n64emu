#pragma once
#include <stdbool.h>
#include <libretro.h>
#ifdef __cplusplus
extern "C" {
#endif

enum controller_pak_option { CONTROLLER_PAK_NONE, CONTROLLER_PAK_MEMORY,
    CONTROLLER_PAK_RUMBLE, CONTROLLER_PAK_TRANSFER };
struct renderer_settings {
    /* Cycle-based timing selects full CPU cache contents and interlocks.
     * Component clocks remain active when CPU timing is off. */
    bool per_cycle_timing;
    bool component_timing;
    unsigned threads, upscale;
    bool vi_filter, dedither, blur, overscan, bob;
    bool native_lod;
};
extern struct renderer_settings renderer_settings;
/* Simplified CPU timing retains work-derived RSP/RDP deadlines. */
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
