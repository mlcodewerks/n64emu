/* Libretro/RCP bridge. Keep the host interface while replacing the rasterizer. */
#include "softrdp_video.h"
#include "presenter.h"
#include "messages.h"
#include "upstream/core/sr.h"
#include "upstream/core/sr_threads.h"
#include "upstream/present/sr_field_output.h"
#include "simd.h"
#include "dps_model.h"
#include "adapter_options.h"
#include <stdlib.h>
#include <string.h>

static struct n64video_config config;
static sr_context *context;
static uint8_t hidden[SR_RDRAM_MAX_SIZE / 2];
static sr_rgba8 *scanout;
static struct rgba *presentation;
static size_t scanout_capacity, presentation_capacity;
static sr_field_output fields;
bool softrdp_native_lod=true;

static void host_interrupt(void *unused)
{ (void)unused; if (config.gfx.mi_intr_cb) config.gfx.mi_intr_cb(); }

void n64video_config_init(struct n64video_config *out)
{ memset(out, 0, sizeof(*out)); out->parallel = true; out->vi.vsync = true; }
void n64video_init(struct n64video_config *in)
{
    n64video_close();
    config = *in;
    softrdp_dps_reset();
    memset(hidden, 4, sizeof(hidden));
    sr_threads_init(config.parallel ? config.num_workers : 1);
    sr_host_interface host = {0};
    host.scale = config.upscale;
    host.workers = sr_threads_count();
    host.rdram = config.gfx.rdram; host.rdram_size = config.gfx.rdram_size;
    host.hidden_rdram = hidden; host.hidden_rdram_size = sizeof(hidden);
    host.dmem = config.gfx.dmem;
    for (unsigned i = 0; i < SR_DP_REGISTER_COUNT; ++i) host.dp_regs[i] = config.gfx.dp_reg[i];
    for (unsigned i = 0; i < SR_VI_REGISTER_COUNT; ++i) host.vi_regs[i] = config.gfx.vi_reg[i];
    host.mi_intr_reg = config.gfx.mi_intr_reg; host.raise_mi_interrupt = host_interrupt;
    host.disable_vi_dither_filter = !config.vi.vi_dedither;
    host.disable_vi_filter = config.vi.mode != VI_MODE_NORMAL;
    host.disable_vi_divot_filter = config.vi.mode != VI_MODE_NORMAL || !config.vi.vi_blur;
    host.disable_vi_aa = config.vi.mode != VI_MODE_NORMAL;
    host.disable_vi_gamma_dither = !config.dithering;
    context = sr_create(&host);
    if (!context) { msg_error("SoftRDP: failed to create renderer"); sr_threads_shutdown(); }
    vdac_init(&config);
}
void n64video_process_list(void)
{
    if (context && sr_process_rdp_list(context) != SR_OK)
        msg_warning("SoftRDP: invalid or unsupported command list");
}

static bool reserve(void **buffer, size_t *capacity, size_t bytes)
{
    if (bytes <= *capacity) return true;
    void *replacement = realloc(*buffer, bytes);
    if (!replacement) return false;
    *buffer = replacement; *capacity = bytes; return true;
}

static bool has_active_bounds(sr_field_bounds bounds)
{
    return bounds.x_begin < bounds.x_end && bounds.y_begin < bounds.y_end;
}

static sr_field_bounds scanout_bounds(const sr_vi_frame_info *info)
{
    sr_field_bounds bounds = {info->active_x_begin, info->active_x_end,
                              info->active_y_begin, info->active_y_end};
    if (!info->interlaced) return bounds;

    /* Crop to the combined active area. VI windows can start on different
     * lines in the two fields, even though the composed image is stationary. */
    bounds.y_begin *= 2;
    bounds.y_end *= 2;
    fields.bounds[info->field] = bounds;
    const sr_field_bounds other = fields.bounds[!info->field];
    if (!has_active_bounds(other)) return bounds;
    if (!has_active_bounds(bounds)) return other;

    if (other.x_begin < bounds.x_begin) bounds.x_begin = other.x_begin;
    if (other.x_end > bounds.x_end) bounds.x_end = other.x_end;
    if (other.y_begin < bounds.y_begin) bounds.y_begin = other.y_begin;
    if (other.y_end > bounds.y_end) bounds.y_end = other.y_end;
    return bounds;
}

void n64video_update_screen(void)
{
    if (!context) return;
    sr_flush(context);
    sr_vi_frame_info info;
    if (sr_get_vi_frame_info(context, &info) != SR_OK || info.hold) return;
    if (!info.width || !info.height || info.width > 8192 || info.height > 8192) return;
    if (!reserve((void **)&scanout, &scanout_capacity, (size_t)info.width * info.height * 4)) return;
    sr_framebuffer frame = {scanout, info.width, info.height, info.width, false};
    if (sr_update_screen(context, &frame) != SR_OK) return;
    if (!frame.valid) memset(scanout, 0, (size_t)info.width * info.height * 4);
    const sr_rgba8 *pixels = scanout;
    uint32_t height = info.height;
    if (!sr_field_output_compose(&fields, pixels, info.width, height, info.width,
            info.interlaced, info.field, config.vi.bob_deinterlace, &pixels, &height)) return;
    struct frame_buffer output = {NULL, info.width, height, height, info.width};
    uint32_t crop_x = 0, crop_y = 0;
    if (config.vi.hide_overscan) {
        const sr_field_bounds bounds = scanout_bounds(&info);
        const uint32_t x0 = bounds.x_begin < info.width ? bounds.x_begin : info.width;
        const uint32_t x1 = bounds.x_end < info.width ? bounds.x_end : info.width;
        const uint32_t y0 = bounds.y_begin < height ? bounds.y_begin : height;
        const uint32_t y1 = bounds.y_end < height ? bounds.y_end : height;
        if(x1>x0 && y1>y0) {
            crop_x=x0;crop_y=y0;
            output.width=x1-x0;output.height=y1-y0;output.height_out=output.height;
        }
    }
    /* Compose fields in core-owned memory, then convert only the visible area
     * directly into this run's frontend buffer. Its contents need not persist. */
    output.pixels = vdac_acquire(output.width, output.height, &output.pitch, false);
    if (!output.pixels) {
        output.pitch = output.width;
        if (!reserve((void **)&presentation, &presentation_capacity,
                (size_t)output.width * output.height * sizeof(*presentation))) return;
        output.pixels = presentation;
    }
    const sr_rgba8 *source = pixels + (size_t)crop_y * info.width + crop_x;
    if (output.width == info.width && output.pitch == info.width)
        sr_bgra_pixels((const uint8_t *)source, (uint8_t *)output.pixels,
                       (size_t)output.width * output.height);
    else
        for (uint32_t y = 0; y < output.height; ++y)
            sr_bgra_pixels((const uint8_t *)(source + (size_t)y * info.width),
                           (uint8_t *)(output.pixels + (size_t)y * output.pitch), output.width);
    if(config.vi.widescreen) output.height_out=output.height_out*3/4;
    vdac_write(&output); vdac_sync(false);
}
void n64video_set_vi_options(bool filter, bool dedither, bool divot, bool overscan, bool bob)
{
    enum vi_mode mode = filter ? VI_MODE_NORMAL : VI_MODE_COLOR;
    if (config.vi.mode == mode && config.vi.vi_dedither == dedither &&
        config.vi.vi_blur == divot && config.vi.hide_overscan == overscan &&
        config.vi.bob_deinterlace == bob) return;
    if (context) sr_set_vi_filters(context, filter, dedither, divot);
    config.vi.mode = mode;
    config.vi.vi_dedither = dedither;
    config.vi.vi_blur = divot;
    config.vi.hide_overscan = overscan;
    config.vi.bob_deinterlace = bob;
    sr_field_output_reset(&fields);
}

void n64video_close(void)
{
    if (context) { sr_flush(context); sr_destroy(context); context = NULL; }
    sr_threads_shutdown(); sr_field_output_reset(&fields);
    free(scanout); free(presentation); scanout = NULL; presentation = NULL;
    scanout_capacity = presentation_capacity = 0;
    vdac_close();
}
unsigned int n64video_noise_state_size(void) { return (unsigned int)sr_state_snapshot_size(); }
void n64video_save_noise(void *data)
{ if (context) {sr_flush(context);sr_save_state(context, data, sr_state_snapshot_size());} else memset(data,0,sr_state_snapshot_size()); }
void n64video_load_noise(const void *data)
{
    if (context && sr_load_state(context,data,sr_state_snapshot_size()) != SR_OK)
        msg_warning("SoftRDP: incompatible renderer state");
    sr_field_output_reset(&fields);
}
uint8_t *n64video_hidden_store(size_t *size) { if (size) *size = sizeof(hidden); return hidden; }
uint32_t n64video_flush_count(void) { return context ? sr_get_debug_stats(context).submitted_batches : 0; }
void *n64video_pixel_domain(void) { return config.gfx.rdram; }
void n64video_resolve(uint32_t a,uint32_t w,uint32_t h,uint32_t s)
{ (void)a;(void)w;(void)h;(void)s; if (context) sr_flush(context); }
void n64video_resolve_for_display(uint32_t a) { (void)a; if (context) sr_flush(context); }
int n64video_drawn_image_for(uint32_t a,uint32_t *base,uint32_t *width)
{
    if (!context) return 0;
    sr_debug_stats s = sr_get_debug_stats(context);
    if (a < s.color_image_address || a-s.color_image_address>=s.color_image_bytes || !s.color_image_width) return 0;
    if (base) *base=s.color_image_address; if (width) *width=s.color_image_width; return 1;
}
void n64video_set_native_lod(bool enable)
{ if(context) sr_flush(context); softrdp_native_lod=enable; }
void n64video_dps_arm(void)
{ if (context) sr_flush(context); softrdp_dps_armed=1; }
int n64video_dps_take(uint32_t words[32])
{ if (context) sr_flush(context); return softrdp_dps_take(sr_threads_count(),words); }
