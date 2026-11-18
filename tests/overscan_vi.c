/* Verify the production VI bounds consumed by the overscan crop adapter. */
#include <assert.h>
#include <stdio.h>
#include "../src/device/rcp/rdp/softrdp/upstream/core/vi.c"

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
    return 0;
}
