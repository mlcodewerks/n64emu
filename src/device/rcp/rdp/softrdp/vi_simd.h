#ifndef PARALLAX_SOFTRDP_VI_SIMD_H
#define PARALLAX_SOFTRDP_VI_SIMD_H
#include "simd.h"

#ifdef SR_PACKED
static inline __m128i sr_vi_dither4(__m128i rgba, __m128i noise)
{
    const __m128i r = _mm_and_si128(noise, _mm_set1_epi32(1));
    const __m128i g = _mm_slli_epi32(_mm_and_si128(noise, _mm_set1_epi32(2)), 7);
    const __m128i b = _mm_slli_epi32(_mm_and_si128(noise, _mm_set1_epi32(4)), 14);
    return _mm_adds_epu8(rgba, _mm_or_si128(r, _mm_or_si128(g,b)));
}
#endif

/* Exact VI horizontal 2x interpolation with gamma disabled. A half-step is
 * ceil((a+b)/2); saturation adds the same per-channel noise bits as scalar VI.
 * begin/end and noise indices are absolute, preserving crop/worker phases. */
static inline int sr_vi_half_dither(const uint8_t *source, uint32_t source_width,
    uint32_t start, uint32_t step, uint32_t begin, uint32_t end,
    const uint16_t *noise, uint8_t *destination)
{
    if (step!=512 || (start&511) || !source_width) return 0;
    uint32_t x=begin;
    while (x<end) {
        const uint32_t coordinate=start+x*512;
        uint32_t at=coordinate>>10;
#ifdef SR_PACKED
        if (!(coordinate&512) && end-x>=8 && at<source_width && source_width-at>4) {
            const __m128i a=_mm_loadu_si128((const __m128i *)(source+4*at));
            const __m128i b=_mm_loadu_si128((const __m128i *)(source+4*at+4));
            const __m128i average=_mm_avg_epu8(a,b);
            const __m128i n=_mm_loadu_si128((const __m128i *)(noise+x));
            const __m128i low=sr_vi_dither4(_mm_unpacklo_epi32(a,average),_mm_unpacklo_epi16(n,_mm_setzero_si128()));
            const __m128i high=sr_vi_dither4(_mm_unpackhi_epi32(a,average),_mm_unpackhi_epi16(n,_mm_setzero_si128()));
            _mm_storeu_si128((__m128i *)(destination+4*x),low);
            _mm_storeu_si128((__m128i *)(destination+4*x+16),high);
            x+=8;
            continue;
        }
#endif
        if (at>=source_width) at=source_width-1;
        const uint32_t next=at+1<source_width?at+1:at;
        for (unsigned c=0;c<4;c++) {
            unsigned color=source[4*at+c];
            if (coordinate&512) color=(color+source[4*next+c]+1)>>1;
            if (c<3) color+=(noise[x]>>c)&1;
            destination[4*x+c]=(uint8_t)(color>255?255:color);
        }
        ++x;
    }
    return 1;
}
#endif
