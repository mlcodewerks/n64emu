#include "renderer_options.h"
#ifndef N64EMU_USF
#include "device/rcp/rdp/performance_cores.h"
#endif
#include <string.h>

struct renderer_settings renderer_settings;
/* Legacy options are supported by every libretro frontend; first value is default.
 * Renderer resources are latched at content load; shared VI controls update live. */
static const struct retro_variable options[] = {
#ifndef N64EMU_USF
    {"n64emu_cycle_timing", "Cycle timing (restart required); on|off"},
#endif
    {"wtfn64_angrylion_deinterlace", "VI deinterlacing; weave|bob"},
    {"wtfn64_vi_filter", "VI filtering; enabled|disabled"},
    {"wtfn64_vi_dedither", "VI dedithering; enabled|disabled"},
    {"wtfn64_vi_blur", "VI divot filter; enabled|disabled"},
    {"wtfn64_crop_overscan", "Crop overscan; enabled|disabled"},
    {"mupen64plus-pak1", "Player 1 controller pak; memory|none|rumble|transfer"},
    {"mupen64plus-pak2", "Player 2 controller pak; none|memory|rumble|transfer"},
    {"mupen64plus-pak3", "Player 3 controller pak; none|memory|rumble|transfer"},
    {"mupen64plus-pak4", "Player 4 controller pak; none|memory|rumble|transfer"},

    {NULL, NULL}
};

static unsigned rendering_thread_count(void)
{
#ifdef N64EMU_USF
    return 1; /* Audio-only builds do not contain renderer topology support. */
#else
    unsigned cores = performance_cores_count();
    return cores > 6 ? 6 : cores ? cores : 1;
#endif
}

void renderer_register_options(retro_environment_t environment)
{
    static struct retro_core_option_v2_definition definitions[64];
    static struct retro_core_option_definition legacy_definitions[64];
    static char descriptions[64][160], values[64][256];
    unsigned version = 0, i;
    environment(RETRO_ENVIRONMENT_GET_CORE_OPTIONS_VERSION, &version);
    if (version >= 1) {
        memset(definitions, 0, sizeof(definitions));
        memset(legacy_definitions, 0, sizeof(legacy_definitions));
        for (i = 0; options[i].key && i < 63; ++i) {
            struct retro_core_option_v2_definition *d = &definitions[i];
            const char *separator = strchr(options[i].value, ';');
            size_t n = (size_t)(separator - options[i].value);
            unsigned j = 0;
            char *p, *next;
            memcpy(descriptions[i], options[i].value, n);
            descriptions[i][n] = 0;
            strcpy(values[i], separator + 2);
            d->key = options[i].key;
            d->desc = descriptions[i];
            d->info = strstr(d->desc, "(restart required)") ?
                "Changes take effect when content is restarted." :
                "Changes take effect while content is running.";
            if (!strcmp(d->key, "n64emu_cycle_timing"))
                d->info = "On uses accurate CPU timing. Off simplifies CPU memory scheduling and disabled floating-point inexact-status reporting for performance. Both retain caches, dependency interlocks, arithmetic results, enabled exceptions and RCP timing. Restart content after changing this option.";
            if (!strcmp(d->key, "wtfn64_vi_filter") || !strcmp(d->key, "wtfn64_vi_dedither") ||
                !strcmp(d->key, "wtfn64_vi_blur"))
                d->info = "Applies immediately to SoftRDP.";
            if (strstr(d->key, "-pak")) {
                d->info = "Choose the accessory attached to this controller. A Memory Pak allows games to save progress.";
            }
            p = values[i];
            do {
                next = strchr(p, '|');
                if (next) *next++ = 0;
                d->values[j++].value = p;
                if (strstr(d->key, "-pak"))
                    d->values[j - 1].label = !strcmp(p, "memory") ? "Memory Pak" :
                        !strcmp(p, "rumble") ? "Rumble Pak" :
                        !strcmp(p, "transfer") ? "Transfer Pak" : "None";
                p = next;
            } while (p && j < RETRO_NUM_CORE_OPTION_VALUES_MAX - 1);
            d->default_value = d->values[0].value;
            legacy_definitions[i].key = d->key;
            legacy_definitions[i].desc = d->desc;
            legacy_definitions[i].info = d->info;
            memcpy(legacy_definitions[i].values, d->values, sizeof(d->values));
            legacy_definitions[i].default_value = d->default_value;
        }
        if (version >= 2) {
            struct retro_core_options_v2 v2 = {NULL, definitions};
            /* SET_CORE_OPTIONS_V2 returns category support, not registration success. */
            environment(RETRO_ENVIRONMENT_SET_CORE_OPTIONS_V2, &v2);
        } else environment(RETRO_ENVIRONMENT_SET_CORE_OPTIONS, legacy_definitions);
        return;
    }
    environment(RETRO_ENVIRONMENT_SET_VARIABLES, (void *)options);
}
static const char *value(retro_environment_t environment, const char *key, const char *fallback)
{
    struct retro_variable var = {key, NULL};
    return environment(RETRO_ENVIRONMENT_GET_VARIABLE, &var) && var.value ? var.value : fallback;
}
enum controller_pak_option controller_read_pak_option(retro_environment_t env, unsigned port)
{
    static const char *keys[] = {"mupen64plus-pak1", "mupen64plus-pak2",
        "mupen64plus-pak3", "mupen64plus-pak4"};
    if (port >= 4) return CONTROLLER_PAK_NONE;
    enum controller_pak_option fallback = port == 0 ? CONTROLLER_PAK_MEMORY : CONTROLLER_PAK_NONE;
    const char *v = value(env, keys[port], port == 0 ? "memory" : "none");
    if (!strcmp(v, "memory")) return CONTROLLER_PAK_MEMORY;
    if (!strcmp(v, "rumble")) return CONTROLLER_PAK_RUMBLE;
    if (!strcmp(v, "transfer")) return CONTROLLER_PAK_TRANSFER;
    if (!strcmp(v, "none")) return CONTROLLER_PAK_NONE;
    return fallback;
}
static bool toggle(retro_environment_t env, const char *key, bool fallback)
{
    const char *v = value(env, key, fallback ? "enabled" : "disabled");
    return !strcmp(v, "enabled") ? true : !strcmp(v, "disabled") ? false : fallback;
}
void renderer_read_options(retro_environment_t env)
{
    struct renderer_settings *s = &renderer_settings;
    memset(s, 0, sizeof(*s));
#ifdef N64EMU_USF
    /* USF snapshots retain the ripper's legacy instruction clock. */
    s->per_cycle_timing = false;
    s->component_timing = false;
#else
    /* Explicit opt-in; obsolete/invalid settings leave Accurate enabled. */
    const char *timing = value(env, "n64emu_cycle_timing", "on");
    s->per_cycle_timing = strcmp(timing, "off") != 0;
    s->relaxed_fp_status = N64EMU_RELAXED_FP_STATUS_AVAILABLE && !s->per_cycle_timing;
    s->component_timing = true;
#endif
    s->threads = rendering_thread_count();
    s->upscale = 1;
    renderer_read_live_options(env);
}

void renderer_read_live_options(retro_environment_t env)
{
    struct renderer_settings *s = &renderer_settings;
    s->native_lod = true;
    s->vi_filter = toggle(env, "wtfn64_vi_filter", true);
    s->dedither = toggle(env, "wtfn64_vi_dedither", true);
    s->blur = toggle(env, "wtfn64_vi_blur", true);
    s->overscan = toggle(env, "wtfn64_crop_overscan", false);
    s->bob = strcmp(value(env, "wtfn64_angrylion_deinterlace", "weave"), "bob") == 0;
}
