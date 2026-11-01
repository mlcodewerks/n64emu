#include "renderer_options.h"
#include <stdlib.h>
#include <string.h>

struct renderer_settings renderer_settings;
#ifdef HAVE_PARALLEL_RDP
#define RENDERERS_BASE "softrdp|parallel-rdp"
#else
#define RENDERERS_BASE "softrdp"
#endif
#ifdef HAVE_GLRDP
#ifdef HAVE_D3D11
#define RENDERERS_MODERN RENDERERS_BASE "|glrdp|dx11"
#else
#define RENDERERS_MODERN RENDERERS_BASE "|glrdp"
#endif
#else
#define RENDERERS_MODERN RENDERERS_BASE
#endif
#ifdef HAVE_GL2_RDP
#define RENDERERS_GL2 RENDERERS_MODERN "|gl2"
#else
#define RENDERERS_GL2 RENDERERS_MODERN
#endif
#ifdef HAVE_D3D9
#define RENDERERS_D3D9 RENDERERS_GL2 "|d3d9"
#else
#define RENDERERS_D3D9 RENDERERS_GL2
#endif
#ifdef HAVE_GLES2_RDP
#define RENDERERS RENDERERS_D3D9 "|gles2"
#else
#define RENDERERS RENDERERS_D3D9
#endif
/* Legacy options are supported by every libretro frontend; first value is default.
 * Renderer resources are latched at content load; shared VI controls update live. */
static const struct retro_variable options[] = {
    {"wtfn64_renderer", "Renderer (restart required); " RENDERERS},
#ifdef HAVE_LEGACY_GPU
    {"wtfn64_legacy_upscale", "Legacy GPU internal upscaling (restart required); 1|2|4"},
#endif
#ifdef HAVE_GLRDP
    {"wtfn64_glrdp_upscale", "GPU RDP internal upscaling (restart required); 1|2|4|8"},
    {"wtfn64_glrdp_blending", "GPU RDP blending accuracy; accurate|fast"},
    {"wtfn64_glrdp_combiner", "GPU RDP combiner accuracy; accurate|fast"},
    {"wtfn64_glrdp_texture_filter", "GPU RDP texture filtering; native|point"},
    {"wtfn64_glrdp_texcoords", "GPU RDP texture coordinate accuracy; accurate|fast"},
    {"wtfn64_glrdp_shading", "GPU RDP shade interpolation; accurate|fast"},
    {"wtfn64_glrdp_shader_specialization", "GPU RDP shader specialization; disabled|enabled"},
    {"wtfn64_glrdp_texture_lod", "GPU RDP texture LOD; enabled|disabled"},
    {"wtfn64_glrdp_color_dither", "GPU RDP RGB dithering; enabled|disabled"},
    {"wtfn64_glrdp_alpha_dither", "GPU RDP alpha dithering; enabled|disabled"},
    {"wtfn64_glrdp_vi_filter", "GPU RDP VI filters; enabled|disabled"},
    {"wtfn64_glrdp_deinterlace", "GPU RDP deinterlacing; weave|bob"},
    {"wtfn64_glrdp_coverage", "GPU RDP coverage sampling; full|reduced|2x|off"},
    {"wtfn64_glrdp_vi_aa", "GPU RDP VI antialiasing; full|reduced|off"},
    {"wtfn64_glrdp_vi_restore", "GPU RDP VI dedithering; enabled|disabled"},
    {"wtfn64_glrdp_vi_divot", "GPU RDP VI divot filter; enabled|disabled"},
    {"wtfn64_glrdp_vi_resample", "GPU RDP VI resampling; enabled|disabled"},
    {"wtfn64_glrdp_vi_gamma", "GPU RDP VI gamma correction; enabled|disabled"},
    {"wtfn64_glrdp_vi_gamma_dither", "GPU RDP VI gamma dithering; enabled|disabled"},
#endif
    {"wtfn64_timing", "CPU cache timing (restart required); accurate|fast|off"},
    {"wtfn64_hle", "HLE level (restart required); Off|RSP HLE"},
    {"wtfn64_angrylion_threads", "CPU RDP rendering threads (restart required); 6|2|4|8|10|auto"},
    {"wtfn64_performance_cores", "Performance cores only (restart required); enabled|disabled"},
    {"wtfn64_angrylion_upscale", "SoftRDP upscaling (restart required); 1|2|4"},
#ifdef HAVE_PARALLEL_RDP
    {"wtfn64_parallel_upscale", "paraLLEl-RDP upscaling (restart required); 1|2|4|8"},
#endif
    {"wtfn64_vi_filter", "VI filtering; enabled|disabled"},
    {"wtfn64_vi_dedither", "VI dedithering; enabled|disabled"},
    {"wtfn64_vi_blur", "VI divot filter; enabled|disabled"},
    {"wtfn64_crop_overscan", "Crop overscan; disabled|enabled"},
    {"wtfn64_angrylion_deinterlace", "SoftRDP deinterlacing; weave|bob"},
    {"wtfn64_native_lod", "Native texture LOD; enabled|disabled"},
    {"mupen64plus-pak1", "Player 1 controller pak; memory|none|rumble|transfer"},
    {"mupen64plus-pak2", "Player 2 controller pak; none|memory|rumble|transfer"},
    {"mupen64plus-pak3", "Player 3 controller pak; none|memory|rumble|transfer"},
    {"mupen64plus-pak4", "Player 4 controller pak; none|memory|rumble|transfer"},
#ifdef HAVE_PARALLEL_RDP
    {"wtfn64_parallel_downscale", "paraLLEl-RDP downsampling steps; 0|1|2|3"},
    {"wtfn64_native_tex_rect", "paraLLEl-RDP native texture rectangles; enabled|disabled"},
    {"wtfn64_ss_readbacks", "paraLLEl-RDP supersampled readbacks (restart required); disabled|enabled"},
    {"wtfn64_ss_dither", "paraLLEl-RDP supersampled dithering (restart required); disabled|enabled"},
#endif
    {NULL, NULL}
};

void renderer_register_options(retro_environment_t environment)
{
    static struct retro_core_option_v2_definition definitions[64];
    static struct retro_core_option_definition legacy_definitions[64];
    static char descriptions[64][160], values[64][256];
    static struct retro_core_option_v2_category categories[] = {
        {"emulation", "Emulation", "CPU core, RSP task routing and accuracy controls. Changes require restarting content."},
        {"video", "Video", "Renderer selection and shared presentation options."},
        {"input", "Controllers", "Controller accessories."},
#ifdef HAVE_GLRDP
        {"glrdp", "GPU RDP", "RDP accuracy and VI controls for GLRDP and DX11."},
#endif
        {"angrylion", "SoftRDP", "Software RDP rendering and synchronization."},
#ifdef HAVE_PARALLEL_RDP
        {"parallel", "paraLLEl-RDP", "Vulkan RDP rendering."},
#endif
        {NULL, NULL, NULL}
    };
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
            d->category_key = "video";
            if(!strcmp(d->key,"wtfn64_renderer"))
                d->info="GLRDP, DX11, GL2, GLES2 and D3D9 render RDP primitives on the GPU. Legacy APIs use dedicated shaders and framebuffer workarounds. Restart content to apply.";
            if (strstr(d->key, "angrylion")) d->category_key = "angrylion";
            else if (strstr(d->key, "glrdp")) d->category_key = "glrdp";
            else if (strstr(d->key, "parallel") || strstr(d->key, "ss_") || strstr(d->key, "native_tex_rect")) d->category_key = "parallel";
            else if (strstr(d->key, "rsp_") || strstr(d->key, "lle_") || strstr(d->key, "_timing") || strstr(d->key, "wtfn64_cpu_") || !strcmp(d->key, "wtfn64_hle")) d->category_key = "emulation";
            if (!strcmp(d->key, "wtfn64_hle"))
                d->info = "Off executes all RSP programs natively. RSP HLE accelerates supported graphics, audio and task microcode, with native fallback for unsupported programs. Works with every CPU cache timing setting, including Accurate. Restart content to apply.";
            if (!strcmp(d->key, "wtfn64_glrdp_blending"))
                d->info = "Accurate uses the RDP divider and quantized blend factors. Fast uses arithmetic division with the same colour sources and factors; translucent edges may differ. Applies immediately.";
            if (!strcmp(d->key, "wtfn64_glrdp_combiner"))
                d->info = "Accurate preserves signed nine-bit combiner arithmetic and rounding. Fast clamps the colour equation and uses eight-bit normalized factors; lighting and sharpening may differ. Both retain both cycles and alpha tests. Applies immediately.";
            if (!strcmp(d->key, "wtfn64_glrdp_vi_filter"))
                d->info = "Disable all GLRDP VI filters, including resampling, gamma and gamma dithering. Individual selections are retained and apply when enabled. Independent of RDP coverage and blending. Applies immediately.";
            if (!strcmp(d->key, "wtfn64_glrdp_deinterlace"))
                d->info = "Weave retains alternating VI fields for stable interlaced detail. Bob duplicates the latest field for less motion combing at half vertical detail. Independent of VI filtering. Applies immediately.";
            if (!strcmp(d->key, "wtfn64_glrdp_texture_filter"))
                d->info = "Native follows the game's RDP texture filtering. Point skips multi-tap texture filtering for lower GPU cost and a sharper, pixelated image. Independent of VI resampling. Applies immediately.";
            if (!strcmp(d->key, "wtfn64_glrdp_texcoords"))
                d->info = "Accurate uses native fixed-point span coordinates and the RDP perspective divider at 1x where supported. Fast uses floating-point planes and perspective division; texture alignment and wobble can differ. Applies immediately.";
            if (!strcmp(d->key, "wtfn64_glrdp_shading"))
                d->info = "Accurate uses native fixed-point span and coverage corrections for vertex colours. Fast uses floating-point colour planes; gradients and alpha thresholds can differ. Independent of combiner arithmetic and texture coordinates. Applies immediately.";
            if (!strcmp(d->key, "wtfn64_glrdp_shader_specialization"))
                d->info = "Compile a shader for the selected coverage and accuracy settings. Applies immediately; changing these settings can pause briefly while the shader compiles. Disabling uses the generic shader.";
            if (!strcmp(d->key, "wtfn64_glrdp_texture_lod"))
                d->info = "Allow the game's per-pixel mip level and LOD fraction calculation. Disabled uses base tiles and skips dynamic LOD; distant textures may shimmer and detail or sharpening effects can change. Applies immediately.";
            if (!strcmp(d->key, "wtfn64_glrdp_color_dither"))
                d->info = "Allow the game's RGB dithering before framebuffer storage. Disabled reduces shader work but can expose colour banding. Independent of alpha dithering and VI filters. Applies immediately.";
            if (!strcmp(d->key, "wtfn64_glrdp_alpha_dither"))
                d->info = "Allow dither added to pixel and shade alpha. Disabled can change transparency thresholds and fades. Alpha comparison and depth rejection remain enabled. Independent of RGB dithering. Applies immediately.";
            if (!strcmp(d->key, "wtfn64_vi_filter") || !strcmp(d->key, "wtfn64_vi_dedither") ||
                !strcmp(d->key, "wtfn64_vi_blur"))
                d->info = "Applies immediately to SoftRDP and paraLLEl-RDP. GLRDP has independent VI controls in the GPU RDP category.";
            if (!strcmp(d->key, "wtfn64_glrdp_coverage"))
                d->info = "Off treats surviving pixels as fully covered. 2x estimates coverage from two native samples; Reduced uses four; Full uses eight. Lower sample counts can lose thin edges. Alpha, depth and AA-off first-sample tests remain active. Independent of VI filters. Applies immediately.";
            if (!strcmp(d->key, "wtfn64_glrdp_vi_aa"))
                d->info = "Controls the game's requested VI edge reconstruction independently of RDP coverage. Off skips reconstruction; Reduced uses four neighbours; Full uses all six. Does not change resampling or other VI filters. Applies immediately.";
            if (!strcmp(d->key, "wtfn64_glrdp_vi_restore"))
                d->info = "Allow the game's eight-neighbour VI dither restoration filter. Disable to reduce scanout cost and retain the framebuffer's dither pattern. Independent of VI antialiasing. Applies immediately.";
            if (!strcmp(d->key, "wtfn64_glrdp_vi_divot"))
                d->info = "Allow the game's horizontal three-pixel median filter at partially covered edges. Independent of other VI filters. Applies immediately.";
            if (!strcmp(d->key, "wtfn64_glrdp_vi_resample"))
                d->info = "Allow the game's native VI bilinear resampling. Disabled uses point sampling without disabling edge reconstruction, dedithering or divot filtering. Applies immediately.";
            if (!strcmp(d->key, "wtfn64_glrdp_vi_gamma"))
                d->info = "Allow gamma correction when requested by the game. Independent of gamma dithering. Applies immediately.";
            if (!strcmp(d->key, "wtfn64_glrdp_vi_gamma_dither"))
                d->info = "Allow gamma dithering when requested by the game. Independent of gamma correction. Applies immediately.";
            if (!strcmp(d->key, "wtfn64_timing"))
                d->info = "Off disables CPU cache timing. Fast models cache tags and hit/miss timing. Accurate models full CPU cache contents. RSP HLE is selected separately. All settings retain component clocks and work-based RDP timing. Restart content to apply.";
            if (!strcmp(d->key, "wtfn64_rsp_hle"))
                d->info = "Use supported graphics, audio and task HLE, with native RSP fallback for unsupported microcodes. Disable to execute all RSP programs natively. Works with every CPU cache timing setting, including Accurate. Restart content to apply.";
            if (strstr(d->key, "-pak")) {
                d->category_key = "input";
                d->info = "Choose the accessory attached to this controller. A Memory Pak allows games to save progress.";
            }
            p = values[i];
            do {
                next = strchr(p, '|');
                if (next) *next++ = 0;
                d->values[j++].value = p;
                if(!strcmp(d->key,"wtfn64_renderer")) {
                    if(!strcmp(p,"gles2"))d->values[j-1].label="OpenGL ES 2 GPU";
                    if(!strcmp(p,"gl2"))d->values[j-1].label="OpenGL 2 GPU";
                    if(!strcmp(p,"d3d9"))d->values[j-1].label="Direct3D 9 GPU";
                }
                if (!strcmp(d->key, "wtfn64_timing"))
                    d->values[j - 1].label = !strcmp(p, "off") ? "Off" :
                        !strcmp(p, "accurate") ? "Accurate" : "Fast";
                if (strstr(d->key, "-pak"))
                    d->values[j - 1].label = !strcmp(p, "memory") ? "Memory Pak" :
                        !strcmp(p, "rumble") ? "Rumble Pak" :
                        !strcmp(p, "transfer") ? "Transfer Pak" : "None";
                p = next;
            } while (p && j < RETRO_NUM_CORE_OPTION_VALUES_MAX - 1);
            if (!strcmp(d->key, "wtfn64_glrdp_coverage")) {
                d->values[0].label = "8x (8 samples, full)";
                d->values[1].label = "4x (4 samples, reduced)";
                d->values[2].label = "2x (2 samples)";
                d->values[3].label = "Off (full coverage per surviving pixel)";
            }
            if (!strcmp(d->key, "wtfn64_glrdp_vi_aa")) {
                d->values[0].label = "Full (6 neighbours)";
                d->values[1].label = "Reduced (4 neighbours)";
                d->values[2].label = "Off";
            }
            d->default_value = d->values[0].value;
            if (!strcmp(d->key, "wtfn64_hle")) {
                d->values[0].label = "RSP HLE";
                d->values[1].label = "Off";
            }
            legacy_definitions[i].key = d->key;
            legacy_definitions[i].desc = d->desc;
            legacy_definitions[i].info = d->info;
            memcpy(legacy_definitions[i].values, d->values, sizeof(d->values));
            legacy_definitions[i].default_value = d->default_value;
        }
        if (version >= 2) {
            struct retro_core_options_v2 v2 = {categories, definitions};
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
static unsigned scale(retro_environment_t env, const char *key, unsigned maximum)
{
    unsigned n = (unsigned)atoi(value(env, key, "1"));
    return (n == 2 || n == 4 || (n == 8 && maximum == 8)) ? n : 1;
}
void renderer_read_options(retro_environment_t env)
{
    struct renderer_settings *s = &renderer_settings;
    const char *v;
    memset(s, 0, sizeof(*s));
    v = value(env, "wtfn64_timing", "accurate");
    s->per_cycle_timing = strcmp(v, "accurate") == 0;
    s->fast_cpu_timing = strcmp(v, "off") != 0 && strcmp(v, "no-cache") != 0;
    /* Consult the old boolean only when the frontend has no level saved. */
    v = value(env, "wtfn64_hle", "");
    s->hle = (!strcmp(v, "Off") || !strcmp(v, "off")) ? HLE_OFF :
        (!strcmp(v, "RSP HLE") || !strcmp(v, "rsp")) ? HLE_RSP :
        toggle(env, "wtfn64_rsp_hle", false) ? HLE_RSP : HLE_OFF;
    s->rsp_hle = s->hle != HLE_OFF;
    s->lle_graphics = s->lle_audio = !s->rsp_hle;
    s->component_timing = true;
#ifdef N64EMU_USF
    s->component_timing = false;
#endif
#ifdef HAVE_PARALLEL_RDP
    if (strcmp(value(env, "wtfn64_renderer", "softrdp"), "parallel-rdp") == 0)
        s->renderer = RENDERER_PARALLEL;
#endif
#ifdef HAVE_GLES2_RDP
    if (!strcmp(value(env,"wtfn64_renderer","softrdp"),"gles2"))s->renderer=RENDERER_GLES2;
#endif
#ifdef HAVE_GL2_RDP
    if (!strcmp(value(env,"wtfn64_renderer","softrdp"),"gl2"))s->renderer=RENDERER_GL2;
#endif
#ifdef HAVE_LEGACY_GPU
#ifdef HAVE_D3D9
    if (!strcmp(value(env,"wtfn64_renderer","softrdp"),"d3d9"))s->renderer=RENDERER_D3D9;
#endif
#endif
#ifdef HAVE_GLRDP
    if (!strcmp(value(env, "wtfn64_renderer", "softrdp"), "glrdp"))
        s->renderer = RENDERER_GLRDP;
#ifdef HAVE_D3D11
    if (!strcmp(value(env, "wtfn64_renderer", "softrdp"), "dx11"))
        s->renderer = RENDERER_DX11;
#endif

#endif
    v = value(env, "wtfn64_angrylion_threads", "6");
    s->threads = strcmp(v, "auto") == 0 ? 0 : (unsigned)atoi(v);
    if (strcmp(v, "auto") != 0 && (s->threads < 2 || s->threads > 10 || s->threads % 2))
        s->threads = 6;
    s->performance_only = toggle(env, "wtfn64_performance_cores", true);
    s->upscale = s->renderer == RENDERER_SOFTRDP ?
        scale(env, "wtfn64_angrylion_upscale", 4) : scale(env, "wtfn64_parallel_upscale", 8);
    if (renderer_native_gpu(s->renderer))
        s->upscale = scale(env, "wtfn64_glrdp_upscale", 8);
    if(renderer_legacy_gpu(s->renderer))s->upscale=scale(env,"wtfn64_legacy_upscale",4);
    renderer_read_live_options(env);
    s->ss_readbacks = toggle(env, "wtfn64_ss_readbacks", false);
    s->ss_dither = toggle(env, "wtfn64_ss_dither", false);
}

void renderer_read_live_options(retro_environment_t env)
{
    struct renderer_settings *s = &renderer_settings;
#ifdef HAVE_GLRDP
    const char *v;
    s->glrdp = glrdp_default_config();
    s->glrdp.specialize_shaders = toggle(env, "wtfn64_glrdp_shader_specialization", false);
    s->glrdp.blending = !strcmp(value(env, "wtfn64_glrdp_blending", "accurate"), "fast") ?
        GLRDP_ARITHMETIC_FAST : GLRDP_ARITHMETIC_ACCURATE;
    s->glrdp.combiner = !strcmp(value(env, "wtfn64_glrdp_combiner", "accurate"), "fast") ?
        GLRDP_ARITHMETIC_FAST : GLRDP_ARITHMETIC_ACCURATE;
    s->glrdp.vi_filter = toggle(env, "wtfn64_glrdp_vi_filter", true);
    s->glrdp.vi_bob = !strcmp(value(env, "wtfn64_glrdp_deinterlace", "weave"), "bob");
    s->glrdp.texture_filter = strcmp(value(env, "wtfn64_glrdp_texture_filter", "native"), "point") != 0;
    s->glrdp.accurate_texcoords = strcmp(value(env, "wtfn64_glrdp_texcoords", "accurate"), "fast") != 0;
    s->glrdp.accurate_shading = strcmp(value(env, "wtfn64_glrdp_shading", "accurate"), "fast") != 0;
    s->glrdp.texture_lod = toggle(env, "wtfn64_glrdp_texture_lod", true);
    s->glrdp.color_dither = toggle(env, "wtfn64_glrdp_color_dither", true);
    s->glrdp.alpha_dither = toggle(env, "wtfn64_glrdp_alpha_dither", true);
    v = value(env, "wtfn64_glrdp_coverage", "full");
    s->glrdp.coverage = !strcmp(v, "off") ? GLRDP_COVERAGE_OFF :
        !strcmp(v, "2x") ? GLRDP_COVERAGE_2X :
        !strcmp(v, "reduced") ? GLRDP_COVERAGE_REDUCED : GLRDP_COVERAGE_FULL;
    v = value(env, "wtfn64_glrdp_vi_aa", "full");
    s->glrdp.vi_aa = !strcmp(v, "off") ? GLRDP_VI_AA_OFF :
        !strcmp(v, "reduced") ? GLRDP_VI_AA_REDUCED : GLRDP_VI_AA_FULL;
    s->glrdp.vi_restore = toggle(env, "wtfn64_glrdp_vi_restore", true);
    s->glrdp.vi_divot = toggle(env, "wtfn64_glrdp_vi_divot", true);
    s->glrdp.vi_resample = toggle(env, "wtfn64_glrdp_vi_resample", true);
    s->glrdp.vi_gamma = toggle(env, "wtfn64_glrdp_vi_gamma", true);
    s->glrdp.vi_gamma_dither = toggle(env, "wtfn64_glrdp_vi_gamma_dither", true);
#endif
    s->downscale = (unsigned)atoi(value(env, "wtfn64_parallel_downscale", "0"));
    if (s->downscale > 3) s->downscale = 0;
    s->native_lod = toggle(env, "wtfn64_native_lod", true);
    s->native_tex_rect = toggle(env, "wtfn64_native_tex_rect", true);
    s->vi_filter = toggle(env, "wtfn64_vi_filter", true);
    s->dedither = toggle(env, "wtfn64_vi_dedither", true);
    s->blur = toggle(env, "wtfn64_vi_blur", true);
    s->overscan = toggle(env, "wtfn64_crop_overscan", false);
    s->bob = strcmp(value(env, "wtfn64_angrylion_deinterlace", "weave"), "bob") == 0;
}
