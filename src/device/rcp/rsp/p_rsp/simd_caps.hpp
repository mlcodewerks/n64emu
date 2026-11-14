#pragma once
#include <cstdlib>
#include <cstring>
#ifdef N64EMU_RSP_SIMD_DISPATCH
unsigned rsp_simd_runtime_level();
#endif
/* Choose the ISA while compiling a block, outside guest execution. */
static inline unsigned rsp_simd_level()
{
#if defined(RSP_SIMD_FORCE_SSE2)
    return 2;
#elif defined(__aarch64__) || defined(__ARM_NEON) || defined(_M_ARM64) || defined(N64EMU_JIT32_CROSS_ARM)
    return 100; /* NEON */
#elif defined(N64EMU_RSP_SIMD_DISPATCH)
    return rsp_simd_runtime_level();
#elif (defined(__x86_64__) || defined(__i386__)) && defined(__GNUC__)
    /* Standalone JIT tests use the same SSE feature selection as the core. */
    static const unsigned level = [] {
        __builtin_cpu_init();
        unsigned actual=__builtin_cpu_supports("sse4.2") ? 42u : __builtin_cpu_supports("sse4.1") ? 41u :
            __builtin_cpu_supports("ssse3") ? 3u : 2u;
        const char *limit=std::getenv("N64EMU_RSP_SIMD");
        unsigned requested=!limit?actual:!std::strcmp(limit,"sse2")?2u:
            !std::strcmp(limit,"ssse3")?3u:!std::strcmp(limit,"sse41")?41u:
            !std::strcmp(limit,"sse42")?42u:actual;
        return requested<actual?requested:actual;
    }();
    return level;
#elif defined(__SSE4_2__)
    return 42;
#elif defined(__SSE4_1__)
    return 41;
#elif defined(__SSSE3__)
    return 3;
#else
    return 2;
#endif
}
#ifdef N64EMU_RSP_SIMD_DISPATCH
void *rsp_resolve_simd_helper(void *pointer);
#endif
