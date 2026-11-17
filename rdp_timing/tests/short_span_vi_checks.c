/* Pipeline response can overlap scanout contention; memory service still
 * determines completion when its actual reservation exceeds that response. */
#include "test_assert.h"
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "../rdp_timing.h"

/* Frozen Q8 costs from the pre-change policy for unaffected topologies and
 * long spans, and the published response floors for the changed short spans.
 * The long two-cycle same-device tuple includes shared read recovery.
 * Addresses are eight-byte beats, as consumed by buffered_span. */
static unsigned topology_checks(void)
{
    static const struct {
        unsigned cycle, pixels, color, depth, profile, scanout, phase, q8;
    } cases[] = {
        {0,1,0,0,15,4,100000,32312},
        {1,1,0,0,15,4,100000,32006},
        {0,1,0,0x200000,15,4,100000,32949},
        {1,1,0,0x200000,15,4,100000,32544},
        {0,1,0,0x100000,15,0,0,28928},
        {1,1,0,0x100000,15,0,0,34560},
        {0,1,0,0x100000,15,4,100000,28928},
        {1,1,0,0x100000,15,4,100000,38880},
        {0,8,0,0x100000,7,4,700000,28928},
        {1,8,0,0x100000,7,4,700000,38880},
        {0,1,0x100000,0,15,4,100000,28928},
        {0,1,0,0x100000,13,4,100000,18465},
        {0,320,0,0x100000,15,4,100000,340106},
        {1,320,0,0x100000,15,4,100000,344968}
    };
    unsigned i;
    for (i = 0; i < sizeof(cases)/sizeof(cases[0]); ++i) {
        struct rdp_timing t;
        uint32_t vi[14];
        unsigned p = cases[i].profile;
        uint64_t clocks;
        memset(&t, 0, sizeof(t)); memset(vi, 0, sizeof(vi));
        t.fb_size = 2; t.cycle_type = cases[i].cycle;
        t.reject = p >> 4; t.modes = t.reject ? 0x20 : 0;
        if (cases[i].scanout) {
            vi[0] = 2; vi[1] = (cases[i].scanout - 1)*0x100000;
            vi[2] = 320; vi[6] = 525;
            vi[9] = (108u << 16) | 748; vi[10] = (37u << 16) | 511;
            vi[12] = 512; vi[13] = 1024;
            t.vi_regs = vi; t.vi_field_ticks = 783000; t.deadline = cases[i].phase;
        }
        clocks = rdp_timing_buffered_span(&t, 0, cases[i].pixels,
            (uint64_t)cases[i].color*8, (uint64_t)cases[i].depth*8,
            p&1, (p>>1)&1, (p>>2)&1, (p>>3)&1);
        assert(clocks != UINT64_MAX);
        assert(clocks*256 + t.span_fraction == cases[i].q8);
    }
    return i;
}

static uint64_t pipeline_cost(unsigned modes,unsigned cycle,uint64_t color,uint64_t depth,
    unsigned color_read,unsigned depth_read,unsigned color_write,unsigned depth_write,unsigned pixels)
{
    struct rdp_timing t;
    uint64_t clocks;
    memset(&t,0,sizeof(t));t.fb_size=2;t.modes=modes;t.cycle_type=cycle;
    clocks=rdp_timing_buffered_span(&t,0,pixels,color,depth,
        color_read,depth_read,color_write,depth_write);
    assert(clocks!=UINT64_MAX);
    return clocks*256+t.span_fraction;
}

/* The unmeasured shared-device service term must remain hidden when the
 * original response already covers it. Long two-cycle dual reads expose the
 * calibrated turnaround per buffer; one-cycle and single-client controls retain
 * their independently checked costs. Addresses below are bit offsets. */
static unsigned paired_device_checks(void)
{
    const uint64_t paired = UINT64_C(0x800000);
    const uint64_t independent = UINT64_C(0x1000000);
    const unsigned widths[] = {1, 8, 32, 64, 320};
    unsigned checks = 0;
    for(unsigned i=0;i<sizeof(widths)/sizeof(widths[0]);++i) {
        unsigned width=widths[i];
        uint64_t a=pipeline_cost(0,1,0,paired,1,1,1,1,width);
        uint64_t b=pipeline_cost(0,1,0,independent,1,1,1,1,width);
        uint64_t recovery=((width+7)/8)*RDP_SHARED_DEVICE_READ_RECOVERY_Q8;
        if(width<=8) { assert(a==135u*256u); ++checks; }
        if(width==32) {
            assert(a>=135u*256u); ++checks;
            assert(a<=135u*256u+recovery); ++checks;
        }
        if(width>=64) { assert(a==b+recovery); ++checks; }
        assert(pipeline_cost(0,0,0,paired,1,1,1,1,width)==
               pipeline_cost(0,0,0,independent,1,1,1,1,width)); ++checks;
        assert(pipeline_cost(0,1,0,paired,1,0,1,0,width)==
               pipeline_cost(0,1,0,independent,1,0,1,0,width)); ++checks;
        assert(pipeline_cost(0,1,0,paired,0,1,1,1,width)==
               pipeline_cost(0,1,0,independent,0,1,1,1,width)); ++checks;
    }
    {
        struct rdp_timing t;
        memset(&t,0,sizeof(t));t.fb_size=2;t.cycle_type=1;
        /* Mixed-bank partial buffers retain their conservative fallback. */
        assert(rdp_timing_buffered_span(&t,0,8,UINT64_C(0x800000)-64,
            independent,1,1,1,1)==UINT64_MAX); ++checks;
    }
    return checks;
}

static unsigned parallel_pipeline_checks(void)
{
    const uint64_t color=UINT64_C(0x1800000),depth=UINT64_C(0x800000);
    unsigned checks=0;
    uint64_t serial=pipeline_cost(0,0,color,depth,0,1,1,1,8);
    uint64_t parallel=pipeline_cost(0x1000,0,color,depth,0,1,1,1,8);
    assert(parallel<serial);++checks;
    /* The interpenetrating mode also consumes post-combiner coverage. */
    assert(pipeline_cost(0x1400,0,color,depth,0,1,1,1,8)==parallel);++checks;
    /* A shared device, an early test or a different Z mode keeps its schedule. */
    assert(pipeline_cost(0x1000,0,color,UINT64_C(0x1000000),0,1,1,1,8)==
           pipeline_cost(0,0,color,UINT64_C(0x1000000),0,1,1,1,8));++checks;
    assert(pipeline_cost(0x1800,0,color,depth,0,1,1,1,8)==serial);++checks;
    assert(pipeline_cost(0x1c00,0,color,depth,0,1,1,1,8)==serial);++checks;
    assert(pipeline_cost(0x1000,1,color,depth,0,1,1,1,8)==
           pipeline_cost(0,1,color,depth,0,1,1,1,8));++checks;
    /* The late-depth overlap requires both stores and no framebuffer fetch. */
    assert(pipeline_cost(0x1000,0,color,depth,0,1,1,0,8)==
           pipeline_cost(0,0,color,depth,0,1,1,0,8));++checks;
    assert(pipeline_cost(0x1000,0,color,depth,1,1,1,1,8)==
           pipeline_cost(0,0,color,depth,1,1,1,1,8));++checks;
    /* Feedback can cover read fill only without the earlier AA dependency. */
    serial=pipeline_cost(0,0,color,depth,1,1,1,1,8);
    assert(pipeline_cost(0x00504070,0,color,depth,1,1,1,1,8)<serial);++checks;
    assert(pipeline_cost(0x00504078,0,color,depth,1,1,1,1,8)==serial);++checks;
    /* Fog/shade alpha and non-memory blend inputs do not consume this path. */
    assert(pipeline_cost(0x05504270,0,color,depth,1,1,1,1,8)==serial);++checks;
    assert(pipeline_cost(0x08504070,0,color,depth,1,1,1,1,8)==serial);++checks;
    assert(pipeline_cost(0x4000,0,color,depth,1,1,1,1,8)==serial);++checks;
    assert(pipeline_cost(0x00544070,0,color,depth,1,1,1,1,8)==serial);++checks;
    /* Compare-only/translucent/decal paths lack this dual-writeback overlap. */
    assert(pipeline_cost(0x00504a50,0,color,depth,1,1,1,0,8)==
           pipeline_cost(0,0,color,depth,1,1,1,0,8));++checks;
    assert(pipeline_cost(0x00504e50,0,color,depth,1,1,1,0,8)==
           pipeline_cost(0,0,color,depth,1,1,1,0,8));++checks;
    assert(pipeline_cost(0x00504870,0,color,depth,1,1,1,1,8)==serial);++checks;
    assert(pipeline_cost(0x00504070,1,color,depth,1,1,1,1,8)==
           pipeline_cost(0,1,color,depth,1,1,1,1,8));++checks;
    assert(pipeline_cost(0x00504070,0,color,depth,1,1,0,0,8)==
           pipeline_cost(0,0,color,depth,1,1,0,0,8));++checks;
    /* Partial/aligned buffers remain monotonic and cover pixel/bus work. */
    {
        uint64_t previous=0;
        for(unsigned pixels=1;pixels<=64;++pixels) {
            uint64_t cost=pipeline_cost(0x1000,0,color,depth,0,1,1,1,pixels);
            uint64_t words=(pixels+3)/4*3;
            assert(cost>=previous && cost>=pixels*256 && cost>=words*256);
            previous=cost;++checks;
        }
    }
    return checks;
}

static unsigned shared_bank_checks(void)
{
    struct rdp_timing t;
    uint32_t vi[14] = {0};
    const uint64_t color = UINT64_C(0x310000) * 8;
    const uint64_t depth = UINT64_C(0x360000) * 8;
    unsigned checks = 0;
    memset(&t, 0, sizeof(t));
    vi[0] = 2; vi[1] = 0x300000; vi[2] = 320; vi[6] = 525;
    vi[9] = (108u << 16) | 748; vi[10] = (37u << 16) | 511;
    vi[12] = 512; vi[13] = 1024;
    t.fb_size = 2; t.vi_regs = vi; t.vi_field_ticks = 783000;
    t.deadline = 100000;
    assert(rdp_timing_shared_bank_read_response(&t, 0, color, depth, 1, 1) != 0); ++checks;
    assert(rdp_timing_shared_bank_read_response(&t,
        (uint64_t)2*RDP_SHARED_BANK_READ_RESPONSE*256-1,color,depth,1,1)==
        RDP_SHARED_BANK_READ_RESPONSE); ++checks;
    assert(!rdp_timing_shared_bank_read_response(&t,
        (uint64_t)2*RDP_SHARED_BANK_READ_RESPONSE*256,color,depth,1,1)); ++checks;
    assert(!rdp_timing_shared_bank_read_response(&t, 0, color, depth, 0, 1)); ++checks;
    assert(!rdp_timing_shared_bank_read_response(&t, 0, color, depth, 1, 0)); ++checks;
    assert(!rdp_timing_shared_bank_read_response(&t, 0, color, color + 8, 1, 1)); ++checks;
    assert(!rdp_timing_shared_bank_read_response(&t, 0, color, UINT64_C(0x210000) * 8, 1, 1)); ++checks;
    assert(!rdp_timing_shared_bank_read_response(&t, UINT64_MAX, color, depth, 1, 1)); ++checks;
    vi[1] = 0x100000;
    assert(!rdp_timing_shared_bank_read_response(&t, 0, color, depth, 1, 1)); ++checks;
    /* The current source row, rather than ORIGIN alone, selects the bank. */
    vi[1] = 0x3fff00;
    assert(!rdp_timing_shared_bank_read_response(&t, 0, color, depth, 1, 1)); ++checks;
    vi[1] = 0x300000;
    vi[0] = 0;
    assert(!rdp_timing_shared_bank_read_response(&t, 0, color, depth, 1, 1)); ++checks;
    vi[0] = 2; t.deadline = 0;
    assert(!rdp_timing_shared_bank_read_response(&t, 0, color, depth, 1, 1)); ++checks;
    t.deadline = t.vi_field_ticks - 1;
    assert(!rdp_timing_shared_bank_read_response(&t, 0, color, depth, 1, 1)); ++checks;
    t.deadline = 100000 + t.vi_field_ticks;
    assert(rdp_timing_shared_bank_read_response(&t, 0, color, depth, 1, 1) != 0); ++checks;
    assert(rdp_timing_shared_bank_read_response(&t,
        (uint64_t)2*RDP_SHARED_BANK_READ_RESPONSE*256-1,color,depth,1,1)==
        RDP_SHARED_BANK_READ_RESPONSE); ++checks;
    assert(!rdp_timing_shared_bank_read_response(&t,
        (uint64_t)2*RDP_SHARED_BANK_READ_RESPONSE*256,color,depth,1,1)); ++checks;
    t.vi_field_start = INT64_C(0x100000000);
    t.deadline = t.vi_field_start + 100000;
    assert(rdp_timing_shared_bank_read_response(&t, 0, color, depth, 1, 1) != 0); ++checks;
    assert(rdp_timing_shared_bank_read_response(&t,
        (uint64_t)2*RDP_SHARED_BANK_READ_RESPONSE*256-1,color,depth,1,1)==
        RDP_SHARED_BANK_READ_RESPONSE); ++checks;
    assert(!rdp_timing_shared_bank_read_response(&t,
        (uint64_t)2*RDP_SHARED_BANK_READ_RESPONSE*256,color,depth,1,1)); ++checks;
    t.vi_field_start = t.deadline = 0; t.vi_elapsed = 133334;
    assert(rdp_timing_shared_bank_read_response(&t, 0, color, depth, 1, 1) != 0); ++checks;
    assert(rdp_timing_shared_bank_read_response(&t,
        (uint64_t)2*RDP_SHARED_BANK_READ_RESPONSE*256-1,color,depth,1,1)==
        RDP_SHARED_BANK_READ_RESPONSE); ++checks;
    assert(!rdp_timing_shared_bank_read_response(&t,
        (uint64_t)2*RDP_SHARED_BANK_READ_RESPONSE*256,color,depth,1,1)); ++checks;
    t.vi_regs = NULL;
    assert(!rdp_timing_shared_bank_read_response(&t, 0, color, depth, 1, 1)); ++checks;
    return checks;
}

int main(void)
{
    uint32_t vi[14];
    struct rdp_timing t;
    unsigned cycle, bank, profile, width, checks = 0;
    const unsigned profiles[] = {7, 15, 19};
    memset(vi, 0, sizeof(vi));
    vi[0] = 2; vi[1] = 0x300000; vi[2] = 320; vi[6] = 525;
    vi[9] = (108u << 16) | 748; vi[10] = (37u << 16) | 511;
    vi[12] = 512; vi[13] = 1024;
    for (cycle = 0; cycle < 2; ++cycle) {
        for (bank = 0; bank < 3; ++bank) {
            vi[1] = bank == 0 ? 0x400000 : bank == 1 ? 0x200000 : 0x300000;
            for (profile = 0; profile < sizeof(profiles)/sizeof(profiles[0]); ++profile) {
                unsigned p = profiles[profile];
                unsigned cr = p & 1, zr = (p >> 1) & 1;
                unsigned cw = (p >> 2) & 1, zw = (p >> 3) & 1;
                unsigned startup = (p & 16) ? 4 + 2*cycle : 15;
                for (width = 1; width <= 8; ++width) {
                    uint64_t work;
                    memset(&t, 0, sizeof(t));
                    t.fb_size = 2; t.cycle_type = cycle;
                    t.vi_regs = vi; t.vi_field_ticks = 783000; t.deadline = 100000;
                    t.reject = (p >> 4) & 1; t.modes = (p & 16) ? 0x20 : 0;
                    work = rdp_timing_buffered_span(&t, 0, width,
                        UINT64_C(0x1000000), UINT64_C(0x1800000), cr, zr, cw, zw);
                    /* At this scanout load each short memory schedule fits
                     * inside the original per-read response floor. */
                    if ((!cycle && (work != 49u*(cr+zr)+startup || t.span_fraction != 0)) ||
                        (cycle && work <= 60u*(cr+zr)+startup)) {
                        fprintf(stderr, "VI inflated pipeline floor: cycle=%u bank=%u profile=%u width=%u got=%llu.%u expected=%u\n",
                            cycle, bank, p, width, (unsigned long long)work,
                            t.span_fraction, (cycle?60u:49u)*(cr+zr)+startup);
                        return 1;
                    }
                    ++checks;
                }
            }
        }
    }
    /* The independently calibrated single-read path keeps its old Q8 cost.
     * It must not inherit the interleaved two-read overlap estimate. */
    vi[1] = 0x300000; t.reject = 0; t.cycle_type = 0; t.span_fraction = 0;
    assert(rdp_timing_buffered_span(&t, 0, 1, UINT64_C(0x800000),
        UINT64_C(0x1000000), 1, 0, 1, 0) == 71);
    assert(t.span_fraction == 130);
    ++checks;
    /* Heavy real memory contention must exceed the latency floor. This is
     * deliberately outside ordinary console scanout settings. */
    vi[0] = 3; vi[9] = 1023; vi[10] = 526; vi[12] = 4095;
    t.span_fraction = 0; t.reject = 0; t.cycle_type = 0;
    assert(rdp_timing_buffered_span(&t, 0, 8, UINT64_C(0x1000000),
        UINT64_C(0x1800000), 1, 1, 1, 1) > 113);
    ++checks;
    checks += topology_checks();
    checks += paired_device_checks();
    checks += shared_bank_checks();
    checks += parallel_pipeline_checks();
    printf("Short-span VI overlap and topology boundaries: %u checks passed\n", checks);
    return 0;
}
