#ifndef PORT_TEST_OBSERVE_H
#define PORT_TEST_OBSERVE_H
#include <stdint.h>

//License: WTFPL
struct port_test_trace {
    uint64_t busy_hash;
    unsigned busy_count, fence_count;
    int64_t first_fence, last_fence;
};
static inline uint64_t port_hash(uint64_t h, uint64_t v)
{
    unsigned n;
    for (n = 0; n < 8; ++n) { h = (h ^ (v & 255)) * UINT64_C(1099511628211); v >>= 8; }
    return h;
}
static inline void port_busy(void *context, int64_t begin, int64_t end, unsigned mask)
{
    struct port_test_trace *trace = (struct port_test_trace *)context;
    trace->busy_hash = port_hash(port_hash(port_hash(trace->busy_hash,
        (uint64_t)begin), (uint64_t)end), mask);
    ++trace->busy_count;
}
static inline void port_fence(void *context, int64_t deadline)
{
    struct port_test_trace *trace = (struct port_test_trace *)context;
    if (!trace->fence_count) trace->first_fence = deadline;
    trace->last_fence = deadline;
    ++trace->fence_count;
}
static inline uint64_t port_state_hash(const struct rdp_timing *t)
{
    uint64_t h = UINT64_C(14695981039346656037);
    unsigned i;
    const uint64_t scalar[] = {
        (uint64_t)t->deadline, t->pos, t->length, t->cycle_type, t->fb_size,
        t->modes, t->left, t->top, t->right, t->bottom, t->tex_size,
        t->tex_width, t->tex_address, t->fb_width, t->fb_address,
        t->scissor_field, t->clock_overhang, t->z_address, t->atomic_primitive,
        t->primitive_color, t->environment_color, t->blend_color, t->fill_color,
        t->combine_hi, t->combine_lo, t->primitive_depth, t->reject,
        t->rdram_bytes, t->validate_uniform_ram, (uint64_t)t->vi_field_start,
        t->vi_field_ticks, t->vi_elapsed
    };
    for (i = 0; i < sizeof(scalar) / sizeof(scalar[0]); ++i) h = port_hash(h, scalar[i]);
    for (i = 0; i < 8; ++i) {
        const struct rdp_uniform_depth *u = t->uniform + i;
        h = port_hash(h, t->words[i]);
        h = port_hash(port_hash(port_hash(port_hash(port_hash(h,
            u->address), u->width), u->height), u->z), u->valid);
    }
    return h;
}
#endif
