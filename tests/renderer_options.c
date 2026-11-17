#include "renderer_options.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
static unsigned version, registered, timing_queries;
static const char *obsolete_timing;
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
        for (; v->key; ++v) { assert(strcmp(v->key, "wtfn64_timing")); ++registered; }
        return true;
    }
    case RETRO_ENVIRONMENT_SET_CORE_OPTIONS: {
        const struct retro_core_option_definition *d = data;
        for (; d->key; ++d) { assert(strcmp(d->key, "wtfn64_timing")); ++registered; }
        return true;
    }
    case RETRO_ENVIRONMENT_SET_CORE_OPTIONS_V2: {
        const struct retro_core_options_v2 *v2 = data;
        assert(!v2->categories || !v2->categories[0].key);
        const struct retro_core_option_v2_definition *d = v2->definitions;
        for (; d->key; ++d) {
            assert(!d->category_key);
            assert(strcmp(d->key, "wtfn64_timing"));
            ++registered;
        }
        return false; /* Lack of category support must not lose registration. */
    }
    case RETRO_ENVIRONMENT_GET_VARIABLE: {
        struct retro_variable *v = data;
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
        renderer_register_options(environment);
        assert(registered == 9);
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
    puts("Flat legacy/v1/v2 options and game/USF timing policy passed, including obsolete saved settings.");
    return 0;
}
