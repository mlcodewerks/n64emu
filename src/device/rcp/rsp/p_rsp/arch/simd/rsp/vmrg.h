//
// arch/x86_64/rsp/vmrg.h
//
// This file is subject to the terms and conditions defined in
// 'LICENSE', which is part of this source code package.
//

static inline __m128i rsp_vmrg(__m128i vs, __m128i vt, __m128i le)
{
#ifdef __SSE4_1__
	return _mm_blendv_epi8(vt, vs, le);
#else
	return rsp_blendv_epi8_fallback(vt, vs, le);
#endif
}
