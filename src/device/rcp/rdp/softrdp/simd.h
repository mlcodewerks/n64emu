#ifndef PARALLAX_SOFTRDP_SIMD_H
#define PARALLAX_SOFTRDP_SIMD_H
#include <stdint.h>
#include <stddef.h>
#include <string.h>

#if !defined(SOFTRDP_SCALAR) && (defined(__aarch64__) || defined(__ARM_NEON))
#include <sse2neon.h>
#define SR_PACKED 1
#define SR_PACKED_MUL32 1
#elif !defined(SOFTRDP_SCALAR) && defined(__SSE2__)
#include <emmintrin.h>
#if defined(__SSE4_1__)
#include <smmintrin.h>
#define SR_PACKED_MUL32 1
#endif
#define SR_PACKED 1
#endif

static inline int softrdp_has_sse42(void)
{
#if defined(SOFTRDP_FORCE_SSE2)
    return 0;
#elif defined(__i386__) || defined(__x86_64__)
    return __builtin_cpu_supports("sse4.2") != 0;
#else
    return 0;
#endif
}

#ifdef SR_PACKED
static inline __m128i sr_mul32(__m128i a, __m128i b)
{
#ifdef SR_PACKED_MUL32
    return _mm_mullo_epi32(a, b);
#else
    __m128i even = _mm_mul_epu32(a, b);
    __m128i odd = _mm_mul_epu32(_mm_srli_si128(a, 4), _mm_srli_si128(b, 4));
    return _mm_unpacklo_epi32(_mm_shuffle_epi32(even, 0x88), _mm_shuffle_epi32(odd, 0x88));
#endif
}
#endif

/* Four RGBA pixels per iteration. Signed 16-bit products cover every legal
 * fraction (0..31), including negative differences and the +16 rounding. */
static inline void sr_blend_bytes(const uint8_t *a, const uint8_t *b,
                                  uint8_t *out, size_t bytes, unsigned fraction)
{
    size_t i = 0;
#ifdef SR_PACKED
    const __m128i zero = _mm_setzero_si128();
    const __m128i f = _mm_set1_epi16((short)fraction), round = _mm_set1_epi16(16);
    for (; i + 16 <= bytes; i += 16) {
        __m128i x = _mm_loadu_si128((const __m128i *)(a + i));
        __m128i y = _mm_loadu_si128((const __m128i *)(b + i));
        __m128i al = _mm_unpacklo_epi8(x, zero), ah = _mm_unpackhi_epi8(x, zero);
        __m128i dl = _mm_sub_epi16(_mm_unpacklo_epi8(y, zero), al);
        __m128i dh = _mm_sub_epi16(_mm_unpackhi_epi8(y, zero), ah);
        dl = _mm_add_epi16(al, _mm_srai_epi16(_mm_add_epi16(_mm_mullo_epi16(dl, f), round), 5));
        dh = _mm_add_epi16(ah, _mm_srai_epi16(_mm_add_epi16(_mm_mullo_epi16(dh, f), round), 5));
        _mm_storeu_si128((__m128i *)(out + i), _mm_packus_epi16(dl, dh));
    }
#endif
    for (; i < bytes; ++i)
        out[i] = (uint8_t)(a[i] + ((((int)b[i] - a[i]) * (int)fraction + 16) >> 5));
}

/* RGBA -> libretro XRGB8888; preserve alpha for the software-framebuffer API. */
/* Four independent byte channels. Both five-bit triangle-filter weights may
 * reach 32; the signed products, their sum and rounding fit in 16 bits. */
static inline uint32_t sr_bilerp_color32(uint32_t base,uint32_t edge_s,uint32_t edge_t,
                                       unsigned frac_s,unsigned frac_t)
{
#ifdef SR_PACKED
    const __m128i zero=_mm_setzero_si128();
    const __m128i a=_mm_unpacklo_epi8(_mm_cvtsi32_si128((int)base),zero);
    const __m128i b=_mm_unpacklo_epi8(_mm_cvtsi32_si128((int)edge_s),zero);
    const __m128i c=_mm_unpacklo_epi8(_mm_cvtsi32_si128((int)edge_t),zero);
    const __m128i ds=_mm_mullo_epi16(_mm_sub_epi16(b,a),_mm_set1_epi16((short)frac_s));
    const __m128i dt=_mm_mullo_epi16(_mm_sub_epi16(c,a),_mm_set1_epi16((short)frac_t));
    const __m128i delta=_mm_srai_epi16(_mm_add_epi16(_mm_add_epi16(ds,dt),_mm_set1_epi16(16)),5);
    return (uint32_t)_mm_cvtsi128_si32(_mm_packus_epi16(_mm_add_epi16(a,delta),zero));
#else
    uint32_t out=0;
    for(unsigned i=0;i<4;i++) {
        int a=(base>>(8*i))&255,b=(edge_s>>(8*i))&255,c=(edge_t>>(8*i))&255;
        int value=a+(((b-a)*(int)frac_s+(c-a)*(int)frac_t+16)>>5);
        if(value<0)value=0;if(value>255)value=255;
        out|=(uint32_t)value<<(8*i);
    }
    return out;
#endif
}

static inline void sr_bgra_pixels(const uint8_t *in, uint8_t *out, size_t pixels)
{
    size_t i = 0;
#ifdef SR_PACKED
    const __m128i ga = _mm_set1_epi32((int)0xff00ff00u);
    const __m128i rb = _mm_set1_epi32(0x00ff00ff);
    for (; i + 4 <= pixels; i += 4) {
        __m128i v = _mm_loadu_si128((const __m128i *)(in + 4*i));
        __m128i q = _mm_or_si128(_mm_and_si128(v, ga),
            _mm_and_si128(_mm_or_si128(_mm_slli_epi32(v,16), _mm_srli_epi32(v,16)),rb));
        _mm_storeu_si128((__m128i *)(out + 4*i), q);
    }
#endif
    for (; i < pixels; ++i) {
        uint8_t r = in[4*i], g = in[4*i+1], b = in[4*i+2], a = in[4*i+3];
        out[4*i] = b; out[4*i+1] = g; out[4*i+2] = r; out[4*i+3] = a;
    }
}

/* The noise hash is modulo 2^32 at every multiply. Process four adjacent
 * positions without changing the absolute x/y/frame phase or scalar tail. */
static inline void sr_noise_block(uint32_t begin, uint32_t end, uint32_t frame_shift,
                                  uint32_t b1_shift, uint32_t a2_shift, uint16_t *out)
{
    uint32_t x = begin;
    const uint32_t prime = 1103515245u;
#ifdef SR_PACKED
    __m128i p = _mm_set1_epi32((int)prime), f = _mm_set1_epi32((int)frame_shift);
    __m128i b = _mm_set1_epi32((int)b1_shift), a = _mm_set1_epi32((int)a2_shift);
    for (; x + 4 <= end; x += 4) {
        __m128i n = _mm_setr_epi32((int)x, (int)x+1, (int)x+2, (int)x+3);
        n = sr_mul32(_mm_xor_si128(f, n), p);
        n = sr_mul32(_mm_xor_si128(b, n), p);
        n = _mm_srli_epi32(sr_mul32(_mm_xor_si128(a, n), p), 16);
        n = _mm_sub_epi32(n, _mm_set1_epi32(32768));
        n = _mm_add_epi16(_mm_packs_epi32(n,n), _mm_set1_epi16((short)0x8000));
        _mm_storel_epi64((__m128i *)(out + x), n);
    }
#endif
    for (; x < end; ++x) {
        uint32_t c = (frame_shift ^ x) * prime;
        uint32_t b = (b1_shift ^ c) * prime;
        out[x] = (uint16_t)(((a2_shift ^ b) * prime) >> 16);
    }
}
#endif
