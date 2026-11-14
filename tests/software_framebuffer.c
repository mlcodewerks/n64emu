/* Exercise the adapter and presenter with rotating, padded frontend buffers. */
#include <assert.h>
#include <stdio.h>
#include <libretro.h>
#include "../src/device/rcp/rdp/softrdp/adapter.c"

static unsigned char buffers[2][1024];
static unsigned turn, queries, submissions, mode;
static void *submitted;
static unsigned submitted_width, submitted_height;
static size_t submitted_pitch;
static sr_vi_frame_info vi;
static unsigned red = 10;
retro_log_printf_t log_cb;
bool libretro_swap_buffer;

static bool environment(unsigned command, void *data)
{
    struct retro_framebuffer *fb = data;
    assert(command == RETRO_ENVIRONMENT_GET_CURRENT_SOFTWARE_FRAMEBUFFER);
    assert(fb->access_flags == RETRO_MEMORY_ACCESS_WRITE);
    ++queries;
    if (mode == 1) return false;
    memset(buffers[turn], 0xcd, sizeof(buffers[turn]));
    fb->data = buffers[turn];
    fb->format = mode == 2 ? RETRO_PIXEL_FORMAT_RGB565 : RETRO_PIXEL_FORMAT_XRGB8888;
    fb->pitch = mode == 3 ? fb->width * 4 - 1 : (fb->width + 3) * 4;
    if (mode == 4) ++fb->width;
    if (mode == 5) fb->data = NULL;
    if (mode == 6) ++fb->pitch;
    return true;
}
static void video(const void *data, unsigned w, unsigned h, size_t pitch)
{
    ++submissions;
    submitted = (void *)data;
    submitted_width = w;
    submitted_height = h;
    submitted_pitch = pitch;
}
retro_environment_t environ_cb = environment;
retro_video_refresh_t video_cb = video;
void softrdp_present(bool new_frame);

void sr_flush(sr_context *ctx) { (void)ctx; }
sr_result sr_get_vi_frame_info(sr_context *ctx, sr_vi_frame_info *info)
{ (void)ctx; *info = vi; return SR_OK; }
sr_result sr_update_screen(sr_context *ctx, sr_framebuffer *out)
{
    (void)ctx;
    for (unsigned y = 0; y < out->height; ++y)
        for (unsigned x = 0; x < out->width; ++x)
            out->pixels[y * out->stride_pixels + x] = (sr_rgba8){red + y, 20 + x, 30, 255};
    out->valid = true;
    return SR_OK;
}
static void render(void)
{
    libretro_swap_buffer = false;
    n64video_update_screen();
    assert(libretro_swap_buffer);
    softrdp_present(true);
}
static void pixel(unsigned x, unsigned y, unsigned r, unsigned g)
{
    unsigned char *p = (unsigned char *)submitted + y * submitted_pitch + x * 4;
    assert(p[0] == 30 && p[1] == g && p[2] == r && p[3] == 255);
}
int main(void)
{
    context = (sr_context *)&vi;
    vi.width = 8; vi.height = 3;
    render();
    assert(submitted == buffers[0] && submitted_pitch == 44);
    assert(presentation == NULL); /* No private presentation buffer or copy. */
    pixel(7, 2, 12, 27);
    assert(buffers[0][32] == 0xcd && buffers[0][43] == 0xcd);
    unsigned before = queries;
    softrdp_present(false);
    assert(!submitted && queries == before);
    softrdp_present(true); /* Expired frontend pointer cannot be resubmitted. */
    assert(!submitted);

    turn = 1;
    config.vi.hide_overscan = true;
    vi.active_x_begin = 2; vi.active_x_end = 7;
    vi.active_y_begin = 1; vi.active_y_end = 3;
    render();
    assert(submitted == buffers[1] && submitted_width == 5 && submitted_height == 2);
    pixel(0, 0, 11, 22); pixel(4, 1, 12, 26);
    assert(buffers[1][20] == 0xcd);

    config.vi.hide_overscan = false;
    vi.interlaced = true; vi.field = false;
    render();
    assert(submitted_height == 6);
    pixel(0, 0, 10, 20); pixel(0, 1, 10, 20);
    turn = 0; red = 50; vi.field = true;
    render();
    assert(submitted == buffers[0]);
    pixel(0, 0, 50, 20); pixel(0, 1, 10, 20); /* Retained opposite field. */
    config.vi.bob_deinterlace = true;
    red = 70;
    render();
    pixel(0, 0, 70, 20); pixel(0, 1, 70, 20);

    for (mode = 1; mode <= 6; ++mode) {
        render();
        assert(submitted == presentation && submitted_pitch == vi.width * 4);
        pixel(0, 0, 70, 20);
    }
    vi.hold = true;
    before = queries;
    n64video_update_screen();
    assert(queries == before);
    sr_field_output_reset(&fields);
    free(scanout); free(presentation);
    vdac_close();
    puts("Software framebuffer: direct conversion, crop, pitch, rotating buffers, weave/bob, fallback and lifetime passed.");
    return 0;
}
