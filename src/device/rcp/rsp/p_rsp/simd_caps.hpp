#pragma once
#ifdef N64EMU_RSP_SIMD_DISPATCH
unsigned rsp_simd_runtime_level();
#endif
/* Only the fallback helpers are multiversioned. Emitted calls resolve once
 * while compiling a block; guest instructions carry no feature-test overhead. */
static inline unsigned rsp_simd_level()
{
#if defined(RSP_SIMD_FORCE_SSE2)
    return 2;
#elif defined(__aarch64__) || defined(__ARM_NEON) || defined(_M_ARM64)
    return 100; /* NEON */
#elif defined(N64EMU_RSP_SIMD_DISPATCH)
    return rsp_simd_runtime_level();
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
