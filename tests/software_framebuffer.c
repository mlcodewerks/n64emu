/* Exercise the adapter and presenter with rotating, padded frontend buffers. */
#include <assert.h>
#include <stdio.h>
#include <libretro.h>
#include "../src/device/rcp/rdp/softrdp/adapter.c"

static unsigned char buffers[2][65536];
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
    fb->pitch = mode == 3 ? fb->width * 4 - 1 :
        mode == 7 ? fb->width * 4 : (fb->width + 3) * 4;
    assert(fb->pitch * fb->height <= sizeof(buffers[turn]));
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
void sr_set_vi_filters(sr_context *ctx, bool filter, bool dedither, bool divot)
{ (void)ctx; (void)filter; (void)dedither; (void)divot; }
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
static void image(unsigned x0, unsigned y0, unsigned w, unsigned h)
{
    assert(submitted_width == w && submitted_height == h);
    unsigned repeat = vi.interlaced ? 2 : 1;
    for (unsigned y = 0; y < h; ++y) {
        for (unsigned x = 0; x < w; ++x)
            pixel(x, y, red + (y0 + y) / repeat, 20 + x0 + x);
        if (submitted == buffers[turn])
            for (size_t b = w * 4; b < submitted_pitch; ++b)
                assert(buffers[turn][y * submitted_pitch + b] == 0xcd);
    }
}
static void check_crops(void)
{
    /* Input active bounds and independently specified expected crop rectangles.
     * Include both copy paths: full-width vertical crops and row-by-row crops. */
    static const unsigned cases[][8] = {
        {2, 7, 1, 3, 2, 1, 5, 2},
        {0, 8, 1, 2, 0, 1, 8, 1},
        {1, 7, 0, 3, 1, 0, 6, 3},
        {0, 8, 0, 3, 0, 0, 8, 3},
        {7, 8, 2, 3, 7, 2, 1, 1},
        {2, 12, 1, 5, 2, 1, 6, 2}, /* Clamp to canvas. */
        {0, 0, 0, 0, 0, 0, 8, 3}, /* Missing/empty/invalid bounds keep canvas. */
        {4, 4, 1, 2, 0, 0, 8, 3},
        {2, 7, 2, 2, 0, 0, 8, 3},
        {7, 2, 1, 2, 0, 0, 8, 3},
        {2, 7, 2, 1, 0, 0, 8, 3},
        {9, 12, 1, 2, 0, 0, 8, 3},
        {2, 7, 4, 5, 0, 0, 8, 3},
    };
    unsigned checks = 0;
    for (unsigned scale = 1; scale <= 4; scale *= 2)
    for (unsigned interlace = 0; interlace <= 1; ++interlace)
    for (unsigned bob = 0; bob <= 1; ++bob)
    for (mode = 0; mode <= 7; ++mode)
    for (unsigned c = 0; c < sizeof(cases) / sizeof(cases[0]); ++c) {
        const unsigned *v = cases[c];
        unsigned repeat = interlace ? 2 : 1;
        vi = (sr_vi_frame_info){0};
        vi.width = 8 * scale; vi.height = 3 * scale;
        vi.interlaced = interlace; vi.field = c & 1;
        vi.active_x_begin = v[0] * scale; vi.active_x_end = v[1] * scale;
        vi.active_y_begin = v[2] * scale; vi.active_y_end = v[3] * scale;
        sr_field_output_reset(&fields);
        n64video_set_vi_options(false, false, false, true, bob);
        for (unsigned toggle = 0; toggle < 3; ++toggle) {
            bool crop = toggle != 1;
            n64video_set_vi_options(false, false, false, crop, bob);
            turn ^= 1; red = 10 + toggle * 40;
            render();
            assert(submitted == ((mode == 0 || mode == 7) ?
                (void *)buffers[turn] : (void *)presentation));
            if (crop)
                image(v[4] * scale, v[5] * scale * repeat,
                      v[6] * scale, v[7] * scale * repeat);
            else
                image(0, 0, vi.width, vi.height * repeat);
            ++checks;
        }
    }
    printf("Overscan: %u pixel-exact crop/toggle checks at 1x/2x/4x, progressive/weave/bob, padded/tight/fallback buffers passed.\n", checks);

    /* Cropping must retain the opposite field at the correct source rows. */
    mode = 0;
    vi = (sr_vi_frame_info){0};
    vi.width = 8; vi.height = 3; vi.interlaced = true;
    vi.active_x_begin = 2; vi.active_x_end = 7;
    vi.active_y_begin = 1; vi.active_y_end = 3;
    n64video_set_vi_options(false, false, false, true, false);
    sr_field_output_reset(&fields);
    red = 10; vi.field = false; render();
    red = 50; vi.field = true; turn ^= 1; render();
    assert(submitted_width == 5 && submitted_height == 4);
    for (unsigned y = 0; y < 4; ++y)
        for (unsigned x = 0; x < 5; ++x)
            pixel(x, y, (y & 1 ? 10 : 50) + 1 + y / 2, 22 + x);

    /* Some ROMs shift the VI window by one field row on alternate fields.
     * Both parities must share the same crop origin and output size. */
    for (unsigned scale = 1; scale <= 4; scale *= 2) {
        sr_field_output_reset(&fields);
        vi.width = 8 * scale;
        vi.height = 4 * scale;
        for (unsigned field = 0; field < 4; ++field) {
            vi.field = field & 1;
            vi.active_x_begin = (vi.field ? 1 : 2) * scale;
            vi.active_x_end = (vi.field ? 6 : 7) * scale;
            vi.active_y_begin = (vi.field ? 0 : 1) * scale;
            vi.active_y_end = (vi.field ? 3 : 4) * scale;
            red = 10 + field * 30;
            turn ^= 1;
            render();
            if (field) {
                assert(submitted_width == 6 * scale && submitted_height == 8 * scale);
                pixel(0, 0, red - (vi.field ? 0 : 30), 20 + scale);
                pixel(0, 1, red - (vi.field ? 30 : 0), 20 + scale);
            }
        }
    }
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
    check_crops();
    sr_field_output_reset(&fields);
    free(scanout); free(presentation);
    vdac_close();
    puts("Software framebuffer: direct conversion, crop, pitch, rotating buffers, weave/bob, fallback and lifetime passed.");
    return 0;
}
