/* Pipeline response can overlap scanout contention; memory service still
 * determines completion when its actual reservation exceeds that response. */
#include "test_assert.h"
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "../rdp_timing.h"

/* Frozen Q8 costs from the pre-change policy for unaffected topologies and
 * long spans, and the published response floors for the changed short spans.
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
        {1,320,0,0x100000,15,4,100000,333448}
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
    printf("Short-span VI overlap and topology boundaries: %u checks passed\n", checks);
    return 0;
}
