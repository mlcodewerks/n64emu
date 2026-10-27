/* The namespace header is force-included for the selected scale. */
#undef kernel_render_span
#ifdef SOFTRDP_FAST_VARIANT
#define kernel_render_span SR_NS(kernel_render_span_sse42)
#else
#define kernel_render_span SR_NS(kernel_render_span_base)
#endif
#include "upstream/core/kernel.c"
