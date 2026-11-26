/* Verify VI bounds and scanout pixels through the production scale variants. */
#include <assert.h>
#include <stdio.h>
#include "../src/device/rcp/rdp/softrdp/upstream/core/vi.c"
#include "../src/device/rcp/rdp/softrdp/upstream/core/rdp_memory.c"

/* Run the same band partition as the VI worker pool in this standalone test. */
void sr_vi_threads_init(uint32_t count) { (void)count; }
uint32_t sr_vi_threads_count(void) { return 3; }
void sr_vi_threads_run(void (*fn)(void *, uint32_t, uint32_t), void *ctx)
{
    for (uint32_t band = 0; band < 3; band++) fn(ctx, band, 3);
}

static uint32_t check_filtered_scanout(void)
{
    enum { RAM_BYTES = 8192, WIDTH = 57 * SOFTRDP_SCALE, HEIGHT = 13 * SOFTRDP_SCALE };
    static uint32_t ram[SR_SCALE_SAMPLES * RAM_BYTES / 4];
    static uint8_t hidden[SR_SCALE_SAMPLES * RAM_BYTES / 2];
    static sr_rgba8 pixels[HEIGHT * (WIDTH + 3)];
    uint32_t random = 0x42ef1983u;
    for (unsigned i = 0; i < sizeof(ram) / sizeof(*ram); i++) {
        random = random * 1664525u + 1013904223u;
        ram[i] = random | (i % 4 ? 0x100e1u : 0);
    }
    for (unsigned i = 0; i < sizeof(hidden); i++)
        hidden[i] = (uint8_t)((i ^ (i >> 3)) & 7u);

    sr_memory memory = { .rdram = (uint8_t *)ram, .rdram_size = RAM_BYTES };
#if SOFTRDP_SCALE > 1
    memory.sample_pixels = memory.rdram + RAM_BYTES;
    memory.sample_hidden = hidden + RAM_BYTES / 2;
#endif
    uint32_t hash = 2166136261u;
    static const unsigned steps[] = {512, 1024, 683, 512, 1024, 512};
    static const unsigned starts[] = {0, 17, 31, 512, 96, 7};
    for (unsigned bpp = 2; bpp <= 4; bpp += 2)
    for (unsigned aa = 0; aa < 4; aa++)
    for (unsigned mode = 0; mode < 4; mode++)
    for (unsigned filter = 0; filter < 4; filter++)
    for (unsigned c = 0; c < sizeof(steps) / sizeof(*steps); c++) {
        const vi_scanout_plan plan = {
            .state = VI_SCANOUT_READY, .bytes_per_pixel = bpp,
            .origin = c == 0 ? bpp : c == 2 ? RAM_BYTES - 3 * bpp : 0x1000,
            .source_stride = 59, .source_width = c == 5 ? 3 : 35,
            .scanout_width = WIDTH, .scanout_height = HEIGHT,
            .active_y_end = HEIGHT / SOFTRDP_SCALE - 1,
            .sample_y_start = 32, .sample_y_add = 512,
            .aa_mode = aa, .output_transform = mode, .dither_frame = 29879,
            .divot_enable = filter & 1u, .dither_filter_enable = filter & 2u,
            .per_row_x = (c & 1u) != 0, .serrate = c == 4,
            .windows = {{starts[c], steps[c], 1, 56, 0}, {163, 1001, 0, 55, aa == 3 ? 32 : 0}},
            .row_window = {[4] = 1, [5] = 1, [6] = 1},
        };
        memory.hidden = c == 2 ? NULL : hidden;
        memset(pixels, 0xa5, sizeof(pixels));
        sr_framebuffer out = { pixels, WIDTH, HEIGHT, WIDTH + 3, false };
        assert(vi_execute_scanout_threaded(&plan, &memory, &out, c & 1u) == SR_OK && out.valid);
        const uint8_t *bytes = (const uint8_t *)pixels;
        for (size_t i = 0; i < sizeof(pixels); i++)
            hash = (hash ^ bytes[i]) * 16777619u;
    }
    return hash;
}

#if SOFTRDP_SCALE > 1
static void check_sample_import(void)
{
    enum { WORDS = 16 };
    uint32_t live[WORDS], reference[WORDS], samples[SR_SCALE_SIDE_SAMPLES * WORDS];
    uint8_t hidden[2 * WORDS], sample_hidden[SR_SCALE_SIDE_SAMPLES * 2 * WORDS];
    for (unsigned i = 0; i < WORDS; i++) {
        live[i] = 10 + i;
        reference[i] = (i >= 5 && i < 11) ? live[i] : 0;
        hidden[2 * i] = (uint8_t)(i & 3u);
        hidden[2 * i + 1] = SR_HIDDEN_CLEAN;
    }
    memset(samples, 0, sizeof(samples));
    memset(sample_hidden, 0x55, sizeof(sample_hidden));
    sr_memory memory = {
        .rdram = (uint8_t *)live, .rdram_size = sizeof(live), .reference = (uint8_t *)reference,
        .sample_pixels = (uint8_t *)samples, .hidden = hidden, .sample_hidden = (uint8_t *)sample_hidden,
    };
    /* Four dirty words, four clean words, a mixed group and a clamped tail. */
    sr_memory_import_foreign(&memory, 4, sizeof(live));
    for (unsigned i = 0; i < WORDS; i++) {
        const bool dirty = i != 0 && (i < 5 || i >= 11);
        assert(reference[i] == (i ? live[i] : 0u));
        for (unsigned s = 0; s < SR_SCALE_SIDE_SAMPLES; s++) {
            assert(samples[s * WORDS + i] == (dirty ? live[i] : 0u));
            for (unsigned j = 0; j < 2; j++)
                assert(sample_hidden[2 * (s * WORDS + i) + j] == (dirty ? hidden[2 * i + j] : 0x55u));
        }
    }
}
#endif

int main(void)
{
    static uint8_t ram[8 * 1024 * 1024];
    sr_memory memory = {0};
    memory.rdram = ram; memory.rdram_size = sizeof(ram);
    /* Raw register windows, followed by expected active canvas coordinates.
     * NTSC and PAL use different blanking offsets and canvas heights. */
    static const unsigned cases[][9] = {
        {525, 108, 748, 34, 514, 8, 633, 0, 240},
        {525, 128, 728, 54, 494, 28, 613, 10, 230},
        {625, 128, 768, 44, 620, 8, 633, 0, 288},
        {625, 148, 748, 64, 600, 28, 613, 10, 278},
        {525, 100, 756, 34, 514, 0, 640, 0, 240}, /* Clamped guard bands. */
        {625, 120, 776, 44, 620, 0, 640, 0, 288},
        {525, 108, 748, 74, 554, 8, 633, 20, 240}, /* Bottom clamp. */
        {625, 128, 768, 84, 660, 8, 633, 20, 288},
    };
    unsigned checks = 0;
    for (unsigned c = 0; c < sizeof(cases) / sizeof(cases[0]); ++c)
    for (unsigned format = 2; format <= 3; ++format)
    for (unsigned interlace = 0; interlace <= 1; ++interlace)
    for (unsigned bypass = 0; bypass <= 1; ++bypass) {
        const unsigned *v = cases[c];
        uint32_t regs[SR_VI_REGISTER_COUNT] = {0};
        regs[SR_VI_STATUS] = format | (interlace ? 0x40u : 0u);
        regs[SR_VI_ORIGIN] = 0x1000; regs[SR_VI_WIDTH] = 320;
        regs[SR_VI_V_SYNC] = v[0];
        regs[SR_VI_H_START] = (v[1] << 16) | v[2];
        regs[SR_VI_V_START] = (v[3] << 16) | v[4];
        regs[SR_VI_X_SCALE] = 512; regs[SR_VI_Y_SCALE] = 1024;
        sr_host_interface host = {0};
        host.disable_vi_filter = bypass;
        for (unsigned r = 0; r < SR_VI_REGISTER_COUNT; ++r) host.vi_regs[r] = &regs[r];
        vi_state vi = {0};
        vi_scanout_plan plan;
        vi_latch_registers(&vi, &host);
        vi_build_scanout_plan(&vi, &memory, &plan);
        assert(plan.state == VI_SCANOUT_READY);
        assert(plan.serrate == (bool)interlace);
        assert(plan.scanout_width == 640 * SOFTRDP_SCALE);
        assert(plan.scanout_height == (v[0] == 625 ? 288 : 240) * SOFTRDP_SCALE);
        assert(plan.active_x_begin == v[5] && plan.active_x_end == v[6]);
        assert(plan.active_y_begin == v[7] && plan.active_y_end == v[8]);
        ++checks;
    }
    printf("Overscan VI bounds: %u NTSC/PAL, RGBA16/32, progressive/interlaced, filter/bypass checks at %ux passed.\n",
           checks, SOFTRDP_SCALE);
    /* Captured from the scalar renderer before the packed row filters. This
     * also covers framebuffer alpha, borders and untouched stride padding. */
    static const uint32_t scanout_hash[] = {0x7a39c39bu, 0xa8db90dfu, 0xb072c3b7u};
    const uint32_t hash = check_filtered_scanout();
    assert(hash == scanout_hash[SR_SCALE_LOG2]);
#if SOFTRDP_SCALE > 1
    check_sample_import();
#endif
    printf("VI scanout and sample checks at %ux passed.\n", SOFTRDP_SCALE);
    return 0;
}
