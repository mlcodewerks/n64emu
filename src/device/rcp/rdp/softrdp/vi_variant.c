#undef vi_execute_scanout_threaded
#ifdef SOFTRDP_FAST_VARIANT
#undef vi_init
#undef vi_latch_registers
#undef vi_build_scanout_plan
#undef vi_execute_scanout
#define vi_init SR_NS(vi_init_sse42)
#define vi_latch_registers SR_NS(vi_latch_registers_sse42)
#define vi_build_scanout_plan SR_NS(vi_build_scanout_plan_sse42)
#define vi_execute_scanout SR_NS(vi_execute_scanout_sse42)
#define vi_execute_scanout_threaded SR_NS(vi_execute_scanout_threaded_sse42)
#else
#define vi_execute_scanout_threaded SR_NS(vi_execute_scanout_threaded_base)
#endif
#include "upstream/core/vi.c"
