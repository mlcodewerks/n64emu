#include "test_assert.h"
#include <stdio.h>
/* Define just one observation macro: other defaults must still compile. */
#define RDP_TIMING_OBSERVE_SPAN(t,p,r) ((void)(t), (void)(p), (void)(r))
#include "../rdp_timing_adapter.h"
#include "observe.h"

static uint64_t buffered_span_cost(struct rdp_timing *t, unsigned left, unsigned right,
    uint64_t color, uint64_t depth, unsigned color_read, unsigned depth_read,
    unsigned color_write, unsigned depth_write)
{
    struct rdp_timing_vi vi;
    rdp_timing_vi_init(&vi, t);
    return rdp_timing_buffered_span(t, &vi, left, right, color, depth,
        color_read, depth_read, color_write, depth_write);
}

static uint64_t buffered_vi_cost(struct rdp_timing *t, uint64_t work, unsigned profile,
    uint64_t color, uint64_t depth, unsigned color_live, unsigned depth_live)
{
    struct rdp_timing_vi vi;
    rdp_timing_vi_init(&vi, t);
    return rdp_timing_buffered_vi(t, &vi, work, profile, color, depth, color_live, depth_live);
}

#ifdef RDP_TIMING_DIFFERENTIAL
extern void port_reference_init(void);
extern void port_reference_vi(const uint32_t *, int64_t, uint32_t);
extern void port_reference_feed(const uint32_t *, unsigned, int64_t);
extern void port_reference_write(uint32_t, uint64_t);
extern uint64_t port_reference_hash(void);
extern struct port_test_trace port_reference_trace(void);
#endif

static int allow_uniform(void *context, const struct rdp_uniform_depth *region)
{ (void)context; return region->z == 0x3ffff; }

static void init(struct rdp_timing_adapter *a, struct port_test_trace *trace)
{
    struct rdp_timing_callbacks cb;
    memset(trace, 0, sizeof(*trace));
    memset(&cb, 0, sizeof(cb));
    cb.context = trace; cb.busy = port_busy; cb.full_sync = port_fence;
    rdp_timing_adapter_init(a, 0, 0x800000, &cb);
}

static void texture_load_checks(void)
{
    struct rdp_timing t;
    memset(&t, 0, sizeof(t));
    t.length = 2;
    t.tex_size = 2;
    t.tex_width = 32;
    t.words[0] = 0x33000000u;
    t.words[1] = (4u << 12) | 2048u; /* Five texels, two TMEM words. */
    assert(rdp_timing_clocks(&t) == 8);
    t.tex_address = 2040;
    assert(rdp_timing_clocks(&t) == 11); /* Second word opens a new RDRAM row. */
    t.tex_address = 0xffff8;
    assert(rdp_timing_clocks(&t) == 13); /* The next word starts in another bank. */
    t.tex_address = 2;
    assert(rdp_timing_clocks(&t) == 9); /* Full final step at an unaligned address. */
    t.words[0] |= 1024u;
    assert(rdp_timing_clocks(&t) == 9); /* LoadBlock source row is ten bits. */
    t.tex_address = 0;
    t.tex_width = 1024;
    t.words[0] = 0x34000000u;
    t.words[1] = (12u << 12) | 4u;
    assert(rdp_timing_clocks(&t) == 11); /* Two separate source rows. */
    t.words[0] = 0x30000000u;
    t.words[1] = 63u << 14;
    assert(rdp_timing_clocks(&t) == 65); /* 64 TMEM writes overlap aligned reads. */
    t.words[1] = 0;
    t.tex_address = 2047;
    assert(rdp_timing_clocks(&t) == 7); /* Odd palette reads rotate in one dword. */

    t.tex_size = 0;
    t.tex_address = 0;
    t.words[0] = 0x33000000u;
    t.words[1] = 63u << 12;
    assert(rdp_timing_clocks(&t) == 17); /* Four texels per CI4 step. */
}

static void raster_work_checks(void)
{
    struct rdp_timing t;
    memset(&t, 0, sizeof(t));
    t.cycle_type = 2;
    t.fb_width = 8;
    t.fb_size = 2;
    assert(rdp_timing_rectangle(&t, 0, 0, 8, 4) == 8);
    t.fb_size = 3;
    assert(rdp_timing_rectangle(&t, 0, 0, 8, 4) == 16);
    t.fb_address = 4;
    t.scissor_field = 3;
    assert(rdp_timing_rectangle(&t, 0, 0, 8, 4) == 10);

    memset(&t, 0, sizeof(t));
    t.length = 8;
    t.fb_width = 8;
    t.fb_size = 2;
    t.right = 32;
    t.bottom = 4;
    t.z_address = 0x100000;
    t.rdram_bytes = 0x800000;
    t.words[0] = 0x08800004u;
    t.words[1] = 4u << 16;
    t.words[6] = 7u << 16;
    t.primitive_depth = 0x40000000;
    t.modes = 0x34; /* Z compare, Z update, primitive Z. */
    rdp_timing_record_uniform(&t, t.z_address, t.fb_width, 1, 0);
    uint64_t rejected = rdp_timing_clocks(&t);
    assert(t.reject && rdp_timing_uniform(&t, t.z_address));
    rdp_timing_invalidate_range(&t, t.z_address, 2);
    assert(rdp_timing_clocks(&t) > rejected && !t.reject);
}

static void blanking_rectangle_checks(void)
{
    uint32_t regs[14] = {0};
    regs[0] = 3;
    regs[2] = 320;
    regs[6] = 524;
    regs[9] = (108u << 16) | 748;
    regs[10] = (37u << 16) | 511;
    regs[12] = 512;
    regs[13] = 1024;
    static const int64_t deadlines[] = {0, 54500, 56000, 760000, 780000};

    /* Exercise a rectangle wholly in blanking, straddling active video,
     * and crossing the field wrap. Cover both interlace parities and all
     * of the framebuffer word-alignment periods. */
    for (unsigned d = 0; d < sizeof(deadlines) / sizeof(deadlines[0]); ++d) {
        for (unsigned mode = 2; mode <= 3; ++mode) {
            for (unsigned size = 0; size <= 3; ++size) {
                for (unsigned field = 0; field <= 3; ++field) {
                    struct rdp_timing t;
                    memset(&t, 0, sizeof(t));
                    t.vi_regs = regs;
                    t.vi_field_ticks = 781251;
                    t.deadline = deadlines[d];
                    t.clock_overhang = 3;
                    t.vi_elapsed = 2;
                    t.cycle_type = mode;
                    t.fb_size = size;
                    t.fb_width = 321;
                    t.fb_address = 5;
                    t.scissor_field = field;

                    struct rdp_timing reference = t;
                    struct rdp_timing_vi vi;
                    rdp_timing_vi_init(&vi, &reference);
                    uint64_t expected = 0;
                    for (unsigned y = 3; y < 244; ++y)
                        expected += rdp_timing_span(&reference, &vi, y, 2, 319);
                    assert(rdp_timing_rectangle(&t, 2, 3, 319, 244) == expected);
                    assert(t.vi_elapsed == reference.vi_elapsed);
                }
            }
        }
    }
}

static void fixed_checks(void)
{
    struct rdp_timing_adapter a, restored;
    struct port_test_trace trace, other;
    struct rdp_timing_callbacks cb;
    uint64_t before;
    unsigned i, j, n = 0;
    uint32_t words[220], vi[14];
    unsigned char be[220 * 4];
    const uint32_t setup[] = {0x3a000000, 0, 0x3a000000, 0, 0x3a000000, 0, 0x3a000000, 0};
    const uint32_t fences[] = {0x29000000, 0, 0x3a000000, 0, 0x29000000, 0};
    const unsigned lengths[] = {8,12,24,28,24,28,40,44};
    init(&a, &trace);
    assert(rdp_timing_adapter_feed_words(&a, setup, 8, 0));
    assert(a.timing.deadline == 3 && !a.timing.clock_overhang);
    /* The first command arrives at COUNT 99.5 and retires at 100.25.
     * A fragmented fence arrives at 100.25 and finishes exactly at 101. */
    init(&a, &trace);
    struct rdp_timing_sync fractional = {0, 0, 0, 0, 0};
    rdp_timing_words_sync_at(&a.timing, setup, 2, 100, 2, &fractional);
    assert(a.timing.deadline == 101 && a.timing.clock_overhang == 3);
    rdp_timing_words_sync_at(&a.timing, fences, 1, 101, 3, &fractional);
    assert(!fractional.seen);
    rdp_timing_words_sync_at(&a.timing, fences + 1, 1, 101, 3, &fractional);
    assert(fractional.seen && fractional.deadline == 101 && !a.timing.clock_overhang);
    /* A submission at 101.5 starts a new interval instead of inheriting
     * the preceding packet's completion time. */
    rdp_timing_words_sync_at(&a.timing, setup, 2, 102, 2, NULL);
    assert(a.timing.deadline == 103 && a.timing.clock_overhang == 3);
    init(&a, &trace);
    assert(rdp_timing_adapter_feed_words(&a, fences, 1, 0));
    assert(!trace.fence_count && a.timing.pos == 1 && !a.timing.deadline);
    assert(rdp_timing_adapter_feed_words(&a, fences + 1, 5, 0));
    assert(trace.fence_count == 2 && trace.first_fence == 1 && trace.last_fence == 3);
    before = port_state_hash(&a.timing);
    assert(!rdp_timing_adapter_feed_be(&a, fences, 3, 0));
    assert(!rdp_timing_adapter_feed_words(&a, NULL, 1, 0));
    assert(!rdp_timing_adapter_feed_words(&a, setup, 8, -1));
    assert(port_state_hash(&a.timing) == before);
    assert(rdp_timing_adapter_feed_words(&a, NULL, 0, 0));
    for (i = 0; i < 8; ++i) {
        assert(rdp_timing_packet_words((8u + i) << 24) == lengths[i]);
        words[n++] = (8u + i) << 24;
        for (j = 1; j < lengths[i]; ++j) words[n++] = j < 8 ? 0 : 0x29000000;
    }
    words[n++] = 0x29000000; words[n++] = 0;
    assert(n == 210);
    for (i = 0; i < n; ++i) {
        be[4*i] = (unsigned char)(words[i] >> 24); be[4*i+1] = (unsigned char)(words[i] >> 16);
        be[4*i+2] = (unsigned char)(words[i] >> 8); be[4*i+3] = (unsigned char)words[i];
    }
    init(&a, &trace);
    assert(rdp_timing_adapter_feed_be(&a, be, n * 4, 0));
    assert(a.timing.deadline == 79 && a.timing.clock_overhang == 1 && trace.fence_count == 1);
    init(&restored, &other);
    for (i = 0; i < n; ++i) assert(rdp_timing_adapter_feed_words(&restored, words + i, 1, 0));
    assert(port_state_hash(&a.timing) == port_state_hash(&restored.timing));
    assert(trace.busy_hash == other.busy_hash && trace.busy_count == other.busy_count);
    /* Save in the middle of a 44-word packet; rebind to a different host. */
    init(&a, &trace);
    words[0] = 0x0f000000;
    for (i = 1; i < 44; ++i) words[i] = 0;
    assert(rdp_timing_adapter_feed_words(&a, words, 5, 0));
    init(&restored, &other);
    restored.timing = a.timing; /* Test snapshot only; host serialization is field-wise. */
    restored.timing.span_fraction = 99; /* Derived packet work is rebuilt after load. */
    cb = restored.callbacks;
    assert(rdp_timing_adapter_after_load(&restored, 0x800000, &cb));
    assert(restored.timing.busy_context == &other && restored.timing.vi_regs == NULL &&
           restored.timing.span_fraction == 0);
    assert(rdp_timing_adapter_feed_words(&a, words + 5, 39, 0));
    assert(rdp_timing_adapter_feed_words(&restored, words + 5, 39, 0));
    assert(port_state_hash(&a.timing) == port_state_hash(&restored.timing));
    assert(trace.busy_hash == other.busy_hash);
    /* Reset clears packet phase; idle gaps clear fractional overhang. */
    init(&a, &trace);
    assert(rdp_timing_adapter_feed_words(&a, setup, 2, 100));
    assert(a.timing.deadline == 101 && a.timing.clock_overhang == 1);
    assert(rdp_timing_adapter_feed_words(&a, setup, 2, 1000));
    assert(a.timing.deadline == 1001 && a.timing.clock_overhang == 1);
    memset(vi, 0, sizeof(vi));vi[0] = 2;
    rdp_timing_adapter_set_vi(&a, vi, 10, 781250);vi[0] = 0;
    assert(a.vi[0] == 2 && a.timing.vi_regs == a.vi && a.timing.vi_field_start == 10);
    /* Restore proofs only with an explicit, coherent-memory validator. */
    rdp_timing_record_uniform(&a.timing, 0x1000, 32, 8, 0x3ffff);
    cb = a.callbacks;cb.validate_uniform = allow_uniform;
    assert(rdp_timing_adapter_after_load(&a, 0x800000, &cb));
    assert(rdp_timing_uniform(&a.timing, 0x1000));
    rdp_timing_adapter_notify_write(&a, 0x1100, 4);
    assert(!rdp_timing_uniform(&a.timing, 0x1000));
    rdp_timing_record_uniform(&a.timing, 0x1000, 32, 8, 0x3ffff);
    cb.validate_uniform = NULL;
    assert(rdp_timing_adapter_after_load(&a, 0x800000, &cb));
    assert(!rdp_timing_uniform(&a.timing, 0x1000));
    a.timing.pos = 44;a.timing.length = 44;
    assert(!rdp_timing_adapter_after_load(&a, 0x800000, &cb));
    init(&a, &trace);
    a.timing.deadline = INT64_MAX - 1;
    assert(rdp_timing_adapter_feed_words(&a, setup, 8, 0));
    assert(a.timing.deadline == INT64_MAX && a.timing.clock_overhang == 0);
}

static void buffered_checks(void)
{
    struct rdp_timing t, other;
    uint32_t vi[14];
    uint64_t aligned, unaligned, active, work = 10000;
    unsigned cycle, width, profile, trials = 0;
    memset(&t, 0, sizeof(t));
    t.fb_size = 2;
    /* Partial transfer words and alignment must affect memory work without
     * discarding Q8 service. Test spans wider than the pixel floor. */
    for (cycle = 0; cycle < 2; ++cycle) {
        for (width = 128; width <= 512; width += 8) {
            t.cycle_type = cycle;t.span_fraction = 0;other = t;
            aligned = buffered_span_cost(&t, 0, width, 0x800000, 0, 1, 0, 1, 0);
            unaligned = buffered_span_cost(&other, 0, width, 0x800010, 0, 1, 0, 1, 0);
            assert(unaligned * 256 + other.span_fraction == aligned * 256 + t.span_fraction + 512);
        }
    }
    t.cycle_type = 0;t.span_fraction = 0;
    assert(buffered_span_cost(&t, 0, 0, 0, 0, 0, 0, 1, 0) == UINT64_MAX);
    assert(buffered_span_cost(&t, 0, 16, 0x7ffff0, 0, 0, 0, 1, 0) == UINT64_MAX);
    /* Independently verify the far-buffer row proof against every actual
     * handoff. It applies to arbitrary span widths and client combinations. */
    for (width = 1; width <= 1024; width += 7) {
        for (profile = 1; profile < 16; ++profile) {
            unsigned cr = profile & 1, zr = (profile >> 1) & 1;
            unsigned cw = (profile >> 2) & 1, zw = (profile >> 3) & 1;
            unsigned back = (!cw && !zw && cr && zr) || (cw && zw && !cr && !zr) ||
                            (cw && zr && !cr && !zw);
            unsigned segments = rdp_timing_buffers(16, 3, width + 3);
            if (!(cr || cw) || !(zr || zw)) continue;
            assert(rdp_timing_row_clocks(0x800030, 0x880030, 16, 3, width + 3, cr, zr, cw, zw) ==
                3u * (segments * ((cr && zr) + zw) + (segments - 1) * back));
            ++trials;
        }
    }
    memset(vi, 0, sizeof(vi));
    vi[0] = 2;vi[1] = 0x100000;vi[2] = 320;vi[6] = 525;
    vi[9] = (108u << 16) | 748;vi[10] = (37u << 16) | 511;
    vi[12] = 512;vi[13] = 1024;
    t.vi_regs = vi;t.vi_field_ticks = 783000;t.span_fraction = 0;
    assert(buffered_vi_cost(&t, work, 5, 0x800000, 0, 1, 0) == work);
    t.deadline = 100000;
    active = buffered_vi_cost(&t, work, 5, 0x800000, 0, 1, 0);
    assert(active > work);
    t.deadline += t.vi_field_ticks;
    assert(buffered_vi_cost(&t, work, 5, 0x800000, 0, 1, 0) == active);
    vi[0] = 0;
    assert(buffered_vi_cost(&t, work, 5, 0x800000, 0, 1, 0) == work);
    /* Deliberately overcommit every halfline. Fractional service must carry
     * across windows, even when each window alone rounds down to zero. */
    vi[0] = 3;vi[9] = 1023;vi[10] = 526;vi[12] = 4095;
    active = buffered_vi_cost(&t, 1, 5, 0x800000, 0, 1, 0);
    assert(active > 1 && active < UINT64_MAX);
    printf("Buffered service: alignment, partial/bank boundaries, %u row proofs, VI phase and saturated grants passed.\n", trials);
}

#ifdef RDP_TIMING_DIFFERENTIAL
static uint32_t random_state = 0x3618acd9;
static uint32_t next_random(void)
{ random_state ^= random_state << 13; random_state ^= random_state >> 17; random_state ^= random_state << 5; return random_state; }

static void differential(void)
{
    struct rdp_timing_adapter a;
    struct port_test_trace trace;
    uint32_t words[44], vi[14];
    unsigned i, j, opcode, length, fragments = 0;
    int64_t submitted = 0;
    const uint32_t modes[] = {0,0x40,0x14,0x34,0x54,0x74,1,0x41};
    const unsigned opcodes[] = {0x3f,0x3d,0x3e,0x2d,0x2f,0x36,0x24,0x25,
        8,9,10,11,12,13,14,15,0x30,0x33,0x34,0x26,0x27,0x28,0x29,0x3a,0x3b,0x39,0x37,0x3c,0x2e};
    init(&a, &trace);port_reference_init();
    memset(vi, 0, sizeof(vi));
    vi[0] = 2;vi[1] = 0x100000;vi[2] = 32;vi[6] = 524;
    vi[9] = (16u << 16) | 80u;vi[10] = (37u << 16) | 511u;vi[12] = vi[13] = 1024;
    for (i = 0; i < 8192; ++i) {
        if (!(i % 64)) {
            const uint32_t *v = (i / 64) & 1 ? vi : NULL;
            rdp_timing_adapter_set_vi(&a, v, 0, 781250);port_reference_vi(v, 0, 781250);
        }
        opcode = opcodes[next_random() % (sizeof(opcodes) / sizeof(opcodes[0]))];
        memset(words, 0, sizeof(words));words[0] = opcode << 24;
        length = rdp_timing_packet_words(words[0]);
        if (opcode == 0x3f || opcode == 0x3d) {
            words[0] |= (next_random() % 4) << 19 | 31;
            words[1] = 0x100000 + (next_random() % 3) * 0x100000;
        } else if (opcode == 0x3e) words[1] = 0x180000 + (next_random() % 3) * 0x80000;
        else if (opcode == 0x2d) words[1] = (128u << 12) | 32u | (next_random() % 4) << 24;
        else if (opcode == 0x2f) {
            words[0] |= (next_random() % 4) << 20 | (next_random() % 2) << 23;
            words[1] = modes[next_random() % (sizeof(modes) / sizeof(modes[0]))];
        } else if (opcode == 0x36 || opcode == 0x24 || opcode == 0x25) words[0] |= (128u << 12) | 32u;
        else if (opcode >= 8 && opcode <= 15) {
            words[0] |= 32u | (1u << 23);words[1] = 16u << 16;
            words[2] = words[6] = 16u << 16;
            words[5] = (next_random() & 1) ? 0xfffffc00u : 0;
            for (j = 8; j < length; ++j) words[j] = next_random();
        } else if (opcode == 0x30 || opcode == 0x33 || opcode == 0x34) words[1] = (28u << 12) | (opcode == 0x30 ? 0 : 12u);
        else words[1] = next_random();
        submitted += i % 137 ? (int64_t)(next_random() % 11) : 10000;
        for (j = 0; j < length;) {
            unsigned n = 1 + next_random() % 13;
            struct port_test_trace ref;
            if (n > length - j) n = length - j;
            trace.fence_count = 0;trace.first_fence = 0;
            assert(rdp_timing_adapter_feed_words(&a, words + j, n, submitted));
            port_reference_feed(words + j, n, submitted);
            ref = port_reference_trace();
            assert(port_state_hash(&a.timing) == port_reference_hash());
            assert(trace.busy_hash == ref.busy_hash && trace.busy_count == ref.busy_count);
            assert((trace.fence_count != 0) == (ref.fence_count != 0));
            if (ref.fence_count) assert(trace.first_fence == ref.first_fence);
            j += n;++fragments;
        }
        if (!(i % 31)) {
            uint32_t address = next_random() & 0x7fffff;
            rdp_timing_adapter_notify_write(&a, address, 256);port_reference_write(address, 256);
        }
    }
    printf("Original-header differential: 8192 commands, %u fragmented windows; state, clocks, busy intervals and fences match.\n", fragments);
}
#endif

int main(void)
{
    texture_load_checks();
    raster_work_checks();
    blanking_rectangle_checks();
    fixed_checks();
    buffered_checks();
    puts("Portable API: clock carry, fences, packet sizes, endian input, restore, VI snapshots, invalidation and overflow passed.");
#ifdef RDP_TIMING_DIFFERENTIAL
    differential();
#endif
    return 0;
}
