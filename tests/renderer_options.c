#include "renderer_options.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
static unsigned version, registered, timing_queries;
static const char *obsolete_timing;
static const char *cycle_timing;
static unsigned timing_registered;
static const char *overscan;
static unsigned crop_registered;
#ifndef N64EMU_USF
unsigned performance_cores_count(void) { return 4; }
#endif
static bool environment(unsigned command, void *data)
{
    switch (command) {
    case RETRO_ENVIRONMENT_GET_CORE_OPTIONS_VERSION:
        *(unsigned *)data = version; return true;
    case RETRO_ENVIRONMENT_SET_VARIABLES: {
        const struct retro_variable *v = data;
        for (; v->key; ++v) {
            assert(strcmp(v->key, "wtfn64_timing")); ++registered;
            if (!strcmp(v->key, "n64emu_cycle_timing")) {
                assert(strstr(v->value, "; on|off")); ++timing_registered;
                assert(strstr(v->value,"|fast")==NULL);
            }
            if (!strcmp(v->key, "wtfn64_crop_overscan")) {
                assert(!strcmp(v->value, "Crop overscan; enabled|disabled"));
                ++crop_registered;
            }
        }
        return true;
    }
    case RETRO_ENVIRONMENT_SET_CORE_OPTIONS: {
        const struct retro_core_option_definition *d = data;
        for (; d->key; ++d) {
            assert(strcmp(d->key, "wtfn64_timing")); ++registered;
            if (!strcmp(d->key, "n64emu_cycle_timing")) {
                assert(!strcmp(d->default_value, "on")); ++timing_registered;
                assert(d->values[2].value==NULL);
            }
            if (!strcmp(d->key, "wtfn64_crop_overscan")) {
                assert(!strcmp(d->default_value, "enabled"));
                ++crop_registered;
            }
        }
        return true;
    }
    case RETRO_ENVIRONMENT_SET_CORE_OPTIONS_V2: {
        const struct retro_core_options_v2 *v2 = data;
        assert(!v2->categories || !v2->categories[0].key);
        const struct retro_core_option_v2_definition *d = v2->definitions;
        for (; d->key; ++d) {
            assert(!d->category_key);
            assert(strcmp(d->key, "wtfn64_timing"));
            if (!strcmp(d->key, "n64emu_cycle_timing")) {
                assert(!strcmp(d->default_value, "on")); ++timing_registered;
                assert(d->values[2].value==NULL);
            }
            ++registered;
            if (!strcmp(d->key, "wtfn64_crop_overscan")) {
                assert(!strcmp(d->default_value, "enabled"));
                ++crop_registered;
            }
        }
        return false; /* Lack of category support must not lose registration. */
    }
    case RETRO_ENVIRONMENT_GET_VARIABLE: {
        struct retro_variable *v = data;
        if (!strcmp(v->key, "n64emu_cycle_timing")) {
            v->value = cycle_timing; return cycle_timing != NULL;
        }
        if (!strcmp(v->key, "wtfn64_crop_overscan")) {
            v->value = overscan; return overscan != NULL;
        }
        if (!strcmp(v->key, "wtfn64_timing")) {
            ++timing_queries; v->value = obsolete_timing; return obsolete_timing != NULL;
        }
        v->value = NULL; return false;
    }
    default: return false;
    }
}
int main(void)
{
    const char *old_values[] = {NULL, "off", "compatible", "fast", "on", "accurate"};
    for (version = 0; version <= 2; ++version) {
        registered = 0;
        crop_registered = 0;
        timing_registered = 0;
        renderer_register_options(environment);
#ifdef N64EMU_USF
        assert(registered == 9 && timing_registered == 0);
#else
        assert(registered == 10 && timing_registered == 1);
#endif
        assert(crop_registered == 1);
        for (unsigned i = 0; i < sizeof(old_values)/sizeof(old_values[0]); ++i) {
            obsolete_timing = old_values[i];
            renderer_read_options(environment);
#ifdef N64EMU_USF
            assert(!renderer_settings.per_cycle_timing && !renderer_settings.component_timing);
#else
            assert(renderer_settings.per_cycle_timing && renderer_settings.component_timing);
#endif
        }
    }
    assert(timing_queries == 0);
    const char *timing_values[] = {NULL, "off", "on", "fast", "invalid"};
    for (unsigned i = 0; i < sizeof(timing_values)/sizeof(timing_values[0]); ++i) {
        cycle_timing = timing_values[i];
        renderer_read_options(environment);
#ifdef N64EMU_USF
        assert(!renderer_settings.per_cycle_timing && !renderer_settings.component_timing);
        assert(!renderer_settings.relaxed_fp_status);
#else
        bool accurate = !cycle_timing || strcmp(cycle_timing, "off");
        bool fixed = N64EMU_RELAXED_FP_STATUS_AVAILABLE && !accurate;
        assert(renderer_settings.per_cycle_timing == accurate && renderer_component_timing());
        assert(renderer_settings.relaxed_fp_status == fixed);
        cycle_timing = accurate ? "off" : "on";
        renderer_read_live_options(environment);
        assert(renderer_settings.per_cycle_timing == accurate && renderer_component_timing());
        assert(renderer_settings.relaxed_fp_status == fixed);
#endif
    }
    const char *crop_values[] = {"enabled", "disabled", "enabled", NULL, "invalid"};
    for (unsigned i = 0; i < sizeof(crop_values) / sizeof(crop_values[0]); ++i) {
        overscan = crop_values[i];
        renderer_read_options(environment);
        assert(renderer_settings.overscan == (overscan && !strcmp(overscan, "enabled")));
        overscan = "disabled";
        renderer_read_live_options(environment);
        assert(!renderer_settings.overscan);
        overscan = "enabled";
        renderer_read_live_options(environment);
        assert(renderer_settings.overscan);
    }
    puts("Crop overscan: legacy/v1/v2 registration, startup and live toggles passed.");
    puts("Flat legacy/v1/v2 options and game/USF timing policy passed, including obsolete saved settings.");
    return 0;
}
