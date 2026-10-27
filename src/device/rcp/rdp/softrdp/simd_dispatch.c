#include "upstream/core/raster.h"
#include "upstream/core/vi.h"
#include "simd.h"

sr_result SR_NS(kernel_render_span_base)(sr_memory *, const rdp_primitive_state *, const rdp_span *);
sr_result SR_NS(vi_execute_scanout_threaded_base)(const vi_scanout_plan *, const sr_memory *, sr_framebuffer *, bool);
#ifdef SOFTRDP_HAVE_SSE42
sr_result SR_NS(kernel_render_span_sse42)(sr_memory *, const rdp_primitive_state *, const rdp_span *);
sr_result SR_NS(vi_execute_scanout_threaded_sse42)(const vi_scanout_plan *, const sr_memory *, sr_framebuffer *, bool);
#endif

sr_result kernel_render_span(sr_memory *memory, const rdp_primitive_state *primitive, const rdp_span *span)
{
#ifdef SOFTRDP_HAVE_SSE42
    /* GCC's SSE4.2 coverage kernel also uses POPCNT. Its CPUID bit can be
     * masked independently in a VM, so require both before selecting it. */
    if (softrdp_has_sse42() && __builtin_cpu_supports("popcnt")) return SR_NS(kernel_render_span_sse42)(memory, primitive, span);
#endif
    return SR_NS(kernel_render_span_base)(memory, primitive, span);
}
sr_result vi_execute_scanout_threaded(const vi_scanout_plan *plan, const sr_memory *memory,
                                    sr_framebuffer *out, bool threaded)
{
#ifdef SOFTRDP_HAVE_SSE42
    if (softrdp_has_sse42() && __builtin_cpu_supports("popcnt")) return SR_NS(vi_execute_scanout_threaded_sse42)(plan,memory,out,threaded);
#endif
    return SR_NS(vi_execute_scanout_threaded_base)(plan,memory,out,threaded);
}
