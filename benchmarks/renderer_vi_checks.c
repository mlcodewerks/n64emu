/* SoftRDP-only options, timing modes and live VI regression checks. */
#include "../src/renderer_options.h"
#include "../src/device/rcp/rdp/performance_cores.h"
#include "../src/device/rcp/rdp/softrdp/upstream/core/sr.h"
#include "../src/device/rcp/rdp/softrdp/upstream/core/sr_threads.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define CHECK(x) do { if (!(x)) { fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #x); exit(1); } } while (0)
retro_log_printf_t log_cb;
bool softrdp_native_lod = true;
static unsigned option_version, retired_queries;
static const char *timing, *filter, *dedither, *divot, *overscan, *bob;
static bool saw_timing_default;
static int retired(const char *key)
{
    return strstr(key,"glrdp") || strstr(key,"legacy") || strstr(key,"parallel") ||
        strstr(key,"ss_") || !strcmp(key,"wtfn64_renderer") ||
        !strcmp(key,"wtfn64_angrylion_upscale") || !strcmp(key,"wtfn64_native_lod") ||
        !strcmp(key,"wtfn64_angrylion_threads") ||
        !strcmp(key,"wtfn64_hle") || !strcmp(key,"wtfn64_rsp_hle") || !strcmp(key,"wtfn64_performance_cores") || !strcmp(key,"wtfn64_off_timing") || !strcmp(key,"wtfn64_native_tex_rect");
}
static void check_default(const char *key,const char *value)
{
    CHECK(!retired(key));
    if (!strcmp(key,"wtfn64_timing")) { CHECK(!strcmp(value,"off")); saw_timing_default=true; }
}
static void check_switch(const char *key,const char *desc,const struct retro_core_option_value *values)
{
    if (!strcmp(key,"wtfn64_timing")) {
        CHECK(!strcmp(desc,"CPU timing (restart required)"));
        CHECK(!strcmp(values[0].value,"off") && !strcmp(values[0].label,"Compatible"));
        CHECK(!strcmp(values[1].value,"on") && !strcmp(values[1].label,"Accurate"));
        CHECK(values[2].value==NULL);
    }
}
static bool environment(unsigned command,void *data)
{
    if (command==RETRO_ENVIRONMENT_GET_CORE_OPTIONS_VERSION) { *(unsigned*)data=option_version; return true; }
    if (command==RETRO_ENVIRONMENT_SET_VARIABLES) {
        for (const struct retro_variable *v=data;v->key;++v) {
            if (!strcmp(v->key,"wtfn64_timing")) CHECK(!strcmp(v->value,"CPU timing (restart required); off|on"));
            const char *first=strchr(v->value,';')+2;
            char value[64];size_t n=strcspn(first,"|");
            CHECK(n<sizeof(value));memcpy(value,first,n);value[n]=0;check_default(v->key,value);
        }
        return true;
    }
    if (command==RETRO_ENVIRONMENT_SET_CORE_OPTIONS) {
        for (const struct retro_core_option_definition *d=data;d->key;++d) { check_default(d->key,d->default_value);check_switch(d->key,d->desc,d->values); }
        return true;
    }
    if (command==RETRO_ENVIRONMENT_SET_CORE_OPTIONS_V2) {
        const struct retro_core_options_v2 *o=data;
        for (const struct retro_core_option_v2_definition *d=o->definitions;d->key;++d) { check_default(d->key,d->default_value);check_switch(d->key,d->desc,d->values); }
        return true;
    }
    if (command==RETRO_ENVIRONMENT_GET_VARIABLE) {
        struct retro_variable *v=data;
        if (retired(v->key)) { ++retired_queries;v->value="disabled";return true; }
        if (!strcmp(v->key,"wtfn64_timing")) v->value=timing;
        else if (!strcmp(v->key,"wtfn64_vi_filter")) v->value=filter;
        else if (!strcmp(v->key,"wtfn64_vi_dedither")) v->value=dedither;
        else if (!strcmp(v->key,"wtfn64_vi_blur")) v->value=divot;
        else if (!strcmp(v->key,"wtfn64_crop_overscan")) v->value=overscan;
        else if (!strcmp(v->key,"wtfn64_angrylion_deinterlace")) v->value=bob;
        else return false;
        return v->value!=NULL;
    }
    return false;
}
static void check_options(void)
{
    for (option_version=0;option_version<=2;++option_version) {
        saw_timing_default=false;
        renderer_register_options(environment);
        CHECK(saw_timing_default);
    }
    renderer_read_options(environment);
    CHECK(!renderer_settings.per_cycle_timing);
    unsigned cores=performance_cores_count();
    unsigned workers=cores>6?6:cores?cores:1;
    CHECK(renderer_settings.threads==workers && renderer_settings.upscale==1);
    const char *modes[]={"on","off"};
    for (unsigned t=0;t<2;++t) {
        timing=modes[t];renderer_read_options(environment);
        CHECK(renderer_settings.per_cycle_timing==(t==0));
        CHECK(renderer_component_timing());
    }
    const char *aliases[]={"cycle","accurate","cycle-based","on","off","no-cache","fast","compatible"};
    for (unsigned i=0;i<8;++i) {
        timing=aliases[i];renderer_read_options(environment);
        CHECK(renderer_settings.per_cycle_timing==(i<4));
    }
    timing="compatible";renderer_read_options(environment);
    timing="fast";filter=dedither=divot="disabled";overscan="enabled";bob="bob";
    renderer_read_live_options(environment);
    CHECK(!renderer_settings.per_cycle_timing);
    CHECK(renderer_settings.threads==workers && renderer_settings.upscale==1);
    CHECK(!renderer_settings.vi_filter && !renderer_settings.dedither && !renderer_settings.blur);
    CHECK(renderer_settings.overscan && renderer_settings.bob && renderer_settings.native_lod);
    CHECK(retired_queries==0);
}

static void check_scanout(unsigned scale, unsigned workers, unsigned format)
{
    const unsigned bytes = 1024 * 1024, origin = 0x1000;
    uint8_t *ram = calloc(1, bytes), *hidden = malloc(bytes / 2);
    CHECK(ram && hidden); memset(hidden, 4, bytes / 2);
    for (unsigned i = 0; i < 320 * 241; ++i)
        ((uint32_t *)(ram + origin))[i] = format == 2 ? 0xfc11fc11 : 0xff8040ff;
    uint32_t regs[SR_VI_REGISTER_COUNT] = {0};
    regs[SR_VI_STATUS] = 0x1001c | format;
    regs[SR_VI_ORIGIN] = origin; regs[SR_VI_WIDTH] = 320;
    regs[SR_VI_V_SYNC] = 525;
    regs[SR_VI_H_START] = ((format == 2 ? 108u : 32u) << 16) | 748u;
    regs[SR_VI_V_START] = (34u << 16) | 514u;
    regs[SR_VI_X_SCALE] = 0x200; regs[SR_VI_Y_SCALE] = 0x400;
    sr_host_interface host = {0};
    host.scale = scale; host.workers = workers;
    host.rdram = ram; host.rdram_size = bytes;
    host.hidden_rdram = hidden; host.hidden_rdram_size = bytes / 2;
    for (unsigned i = 0; i < SR_VI_REGISTER_COUNT; ++i) host.vi_regs[i] = &regs[i];
    sr_threads_init(workers);
    sr_context *ctx = sr_create(&host); CHECK(ctx);
    sr_vi_frame_info info;
    CHECK(sr_get_vi_frame_info(ctx, &info) == SR_OK && info.display);
    sr_rgba8 *pixels = malloc((size_t)info.width * info.height * sizeof(*pixels));
    CHECK(pixels);
    sr_framebuffer out = {pixels, info.width, info.height, info.width, false};
    CHECK(sr_update_screen(ctx, &out) == SR_OK && out.valid);
    const unsigned sample = scale * scale - 1;
    uint32_t retained = sr_debug_read_sample(ctx, origin, sample);
    CHECK(retained != 0);
    size_t state_size = sr_state_snapshot_size();
    void *before = malloc(state_size), *after = malloc(state_size);
    CHECK(before && after);
    for (unsigned cycle = 0; cycle < 4; ++cycle) {
        CHECK(sr_save_state(ctx, before, state_size) == SR_OK);
        sr_set_vi_filters(ctx, false, cycle & 1, cycle & 2);
        CHECK(sr_save_state(ctx, after, state_size) == SR_OK);
        CHECK(!memcmp(before, after, state_size));
        CHECK(sr_debug_read_sample(ctx, origin, sample) == retained);
        /* Also cover the separate horizontal window used for mid-field writes. */
        sr_vi_scanline_regs window = {100, regs[SR_VI_H_START], regs[SR_VI_X_SCALE]};
        CHECK(sr_set_vi_scanline_registers(ctx, &window, 1) == SR_OK);
        CHECK(sr_get_vi_frame_info(ctx, &info) == SR_OK && info.display);
        CHECK(sr_update_screen(ctx, &out) == SR_OK && out.valid);
        for (unsigned y = 0; y < info.height; y += scale) {
            for (unsigned x = info.active_x_begin; x < info.active_x_end; ++x) {
                sr_rgba8 p = pixels[(size_t)y * info.width + x];
                if (p.r != (format == 2 ? 248 : 255) || p.g != 128 || p.b != 64)
                    fprintf(stderr, "scale=%u workers=%u format=%u cycle=%u x=%u y=%u RGB=%u,%u,%u\n",
                            scale, workers, format, cycle, x, y, p.r, p.g, p.b);
                CHECK(p.r == (format == 2 ? 248 : 255) && p.g == 128 && p.b == 64);
            }
        }
        CHECK(regs[SR_VI_STATUS] == (0x1001c | format));
        sr_set_vi_filters(ctx, true, true, true);
        CHECK(sr_get_vi_frame_info(ctx, &info) == SR_OK);
        CHECK(sr_update_screen(ctx, &out) == SR_OK && out.valid);
        CHECK(pixels[96u * scale].g > 128); /* Gamma restored. */
    }
    /* Preserve the real hardware replicate-fetch quirk when requested by ROM. */
    regs[SR_VI_STATUS] = 0x300 | format;
    CHECK(sr_get_vi_frame_info(ctx, &info) == SR_OK);
    CHECK(sr_update_screen(ctx, &out) == SR_OK && out.valid);
    if (scale == 1) CHECK(pixels[format == 2 ? 192 : 160].r == 0);
    sr_destroy(ctx); free(before); free(after); free(pixels); free(hidden); free(ram);
    printf("VI toggle, scanline windows and retained state: %ux, %u workers, RGBA%u passed.\n",
           scale, workers, format == 2 ? 16 : 32);
}

int main(void)
{
    check_options();
    for (unsigned scale = 1; scale <= 4; scale *= 2) {
        for (unsigned format = 2; format <= 3; ++format) {
            check_scanout(scale, 1, format);
            check_scanout(scale, 6, format);
        }
    }
    puts("SoftRDP-only options, default Cycle toggle and live VI checks passed.");
    return 0;
}
