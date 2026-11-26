#ifndef N64EMU_SOFTRDP_VI_SIMD_H
#define N64EMU_SOFTRDP_VI_SIMD_H
#include "simd.h"
#include "upstream/core/rdp_memory.h"

#ifdef SR_PACKED
static inline __m128i sr_vi_load4(const void *pixels)
{
#ifdef SR_NEON
    return vreinterpretq_s64_u8(vld1q_u8(pixels));
#else
    return _mm_loadu_si128((const __m128i *)pixels);
#endif
}

static inline void sr_vi_store4(void *pixels, __m128i value)
{
#ifdef SR_NEON
    vst1q_u8(pixels, vreinterpretq_u8_s64(value));
#else
    _mm_storeu_si128((__m128i *)pixels, value);
#endif
}

static inline __m128i sr_vi_load1(const void *pixel)
{
    int32_t packed;
    memcpy(&packed, pixel, sizeof(packed));
    return _mm_cvtsi32_si128(packed);
}

static inline __m128i sr_vi_load8_bytes(const void *bytes)
{
#ifdef SR_NEON
    return vreinterpretq_s64_u8(vcombine_u8(vld1_u8(bytes), vdup_n_u8(0)));
#else
    return _mm_loadl_epi64((const __m128i *)bytes);
#endif
}

static inline __m128i sr_vi_decode32(__m128i pixels, bool coverage)
{
#ifdef SR_NEON
    pixels = vreinterpretq_s64_u8(vrev32q_u8(vreinterpretq_u8_s64(pixels)));
#elif defined(__SSSE3__)
    const __m128i order = _mm_setr_epi8(3, 2, 1, 0, 7, 6, 5, 4, 11, 10, 9, 8, 15, 14, 13, 12);
    pixels = _mm_shuffle_epi8(pixels, order);
#else
    pixels = _mm_or_si128(_mm_slli_epi16(pixels, 8), _mm_srli_epi16(pixels, 8));
    pixels = _mm_or_si128(_mm_slli_epi32(pixels, 16), _mm_srli_epi32(pixels, 16));
#endif
    if (!coverage) return pixels;
    return _mm_or_si128(_mm_and_si128(pixels, _mm_set1_epi32(0x00ffffff)),
        _mm_and_si128(_mm_srli_epi32(pixels, 5), _mm_set1_epi32(0x07000000)));
}

static inline void sr_vi_decode16(const uint8_t *pixels, const uint8_t *hidden,
                                  uint8_t *out, bool coverage)
{
    __m128i raw = sr_vi_load4(pixels);
#ifdef SR_NEON
    raw = vreinterpretq_s64_u16(vrev32q_u16(vreinterpretq_u16_s64(raw)));
#else
    raw = _mm_shufflelo_epi16(raw, _MM_SHUFFLE(2, 3, 0, 1));
    raw = _mm_shufflehi_epi16(raw, _MM_SHUFFLE(2, 3, 0, 1));
#endif
    const __m128i zero = _mm_setzero_si128();
    const __m128i lsb = _mm_and_si128(raw, _mm_set1_epi16(1));
    const __m128i opaque = _mm_sub_epi16(zero, lsb);
    __m128i alpha = opaque;
    if (coverage) {
        __m128i low = _mm_and_si128(opaque, _mm_set1_epi16(3));
        if (hidden) {
            const __m128i stored = _mm_unpacklo_epi8(sr_vi_load8_bytes(hidden), zero);
            const __m128i clean = _mm_and_si128(stored, _mm_set1_epi16(SR_HIDDEN_CLEAN));
            const __m128i valid = _mm_cmpeq_epi16(clean, zero);
            low = _mm_or_si128(_mm_andnot_si128(valid, low),
                               _mm_and_si128(valid, _mm_and_si128(stored, _mm_set1_epi16(3))));
        }
        alpha = _mm_or_si128(low, _mm_slli_epi16(lsb, 2));
    }
    const __m128i r = _mm_and_si128(_mm_srli_epi16(raw, 8), _mm_set1_epi16(0xf8));
    const __m128i g = _mm_and_si128(_mm_slli_epi16(raw, 5), _mm_set1_epi16((short)0xf800));
    const __m128i b = _mm_and_si128(_mm_slli_epi16(raw, 2), _mm_set1_epi16(0xf8));
    const __m128i rg = _mm_or_si128(r, g);
    const __m128i ba = _mm_or_si128(b, _mm_slli_epi16(alpha, 8));
    sr_vi_store4(out, _mm_unpacklo_epi16(rg, ba));
    sr_vi_store4(out + 16, _mm_unpackhi_epi16(rg, ba));
}

/* Lanes are RGB followed by the coverage byte of each source texel. */
static inline __m128i sr_vi_full_coverage(__m128i pixels)
{
    return _mm_cmpeq_epi32(_mm_srli_epi32(pixels, 24), _mm_set1_epi32(7));
}

static inline __m128i sr_vi_select(__m128i mask, __m128i yes, __m128i no)
{
#ifdef SR_NEON
    return vbslq_s64(vreinterpretq_u64_s64(mask), yes, no);
#elif defined(__SSE4_1__)
    return _mm_blendv_epi8(no, yes, mask);
#else
    return _mm_or_si128(_mm_and_si128(mask, yes), _mm_andnot_si128(mask, no));
#endif
}

static inline __m128i sr_vi_reconstruct_tap(__m128i sum, __m128i low,
                                           __m128i high, __m128i tap)
{
#ifdef SR_NEON
    const uint8x16_t bytes = vreinterpretq_u8_s64(tap);
    const uint8x16_t less = vcgtq_u8(vreinterpretq_u8_s64(low), bytes);
    const uint8x16_t more = vcgtq_u8(bytes, vreinterpretq_u8_s64(high));
    return vreinterpretq_s64_u8(vsubq_u8(vaddq_u8(vreinterpretq_u8_s64(sum), less), more));
#else
    tap = _mm_xor_si128(tap, _mm_set1_epi8((char)0x80));
    return _mm_sub_epi8(_mm_add_epi8(sum, _mm_cmpgt_epi8(low, tap)),
                        _mm_cmpgt_epi8(tap, high));
#endif
}

/* Comparing against the bounds of the centre's 5-bit bin gives -1/0/+1
 * without shifting each of the eight taps. Byte addition wraps like VI. */
static inline __m128i sr_vi_reconstruct4(__m128i center, const uint8_t *above,
                                        const uint8_t *mid, const uint8_t *below)
{
    const __m128i base = _mm_and_si128(center, _mm_set1_epi8((char)0xf8));
#ifdef SR_NEON
    const __m128i low = base;
#else
    const __m128i low = _mm_xor_si128(base, _mm_set1_epi8((char)0x80));
#endif
    const __m128i high = _mm_or_si128(low, _mm_set1_epi8(7));
    __m128i sum = sr_vi_reconstruct_tap(base, low, high, sr_vi_load4(above - 4));
    sum = sr_vi_reconstruct_tap(sum, low, high, sr_vi_load4(above));
    sum = sr_vi_reconstruct_tap(sum, low, high, sr_vi_load4(above + 4));
    sum = sr_vi_reconstruct_tap(sum, low, high, sr_vi_load4(mid - 4));
    sum = sr_vi_reconstruct_tap(sum, low, high, sr_vi_load4(mid + 4));
    sum = sr_vi_reconstruct_tap(sum, low, high, sr_vi_load4(below - 4));
    sum = sr_vi_reconstruct_tap(sum, low, high, sr_vi_load4(below));
    return sr_vi_reconstruct_tap(sum, low, high, sr_vi_load4(below + 4));
}

static inline __m128i sr_vi_aa_adjust(__m128i center, __m128i low,
                                      __m128i high, __m128i coefficient)
{
    const __m128i offset = _mm_sub_epi16(_mm_add_epi16(low, high),
                                        _mm_add_epi16(center, center));
    const __m128i product = _mm_mullo_epi16(offset, coefficient);
    const __m128i delta = _mm_srai_epi16(_mm_add_epi16(product, _mm_set1_epi16(4)), 3);
    return _mm_and_si128(_mm_add_epi16(center, delta), _mm_set1_epi16(255));
}

typedef struct sr_vi_extrema {
    __m128i low, high, second_low, second_high;
} sr_vi_extrema;

static inline void sr_vi_aa_fold(sr_vi_extrema *extrema, __m128i value)
{
    extrema->second_low = _mm_min_epu8(extrema->second_low, _mm_max_epu8(value, extrema->low));
    extrema->second_high = _mm_max_epu8(extrema->second_high, _mm_min_epu8(value, extrema->high));
    extrema->low = _mm_min_epu8(extrema->low, value);
    extrema->high = _mm_max_epu8(extrema->high, value);
}

static inline __m128i sr_vi_aa4(__m128i center, const uint8_t *above,
                               const uint8_t *mid, const uint8_t *below, int fetch_bug)
{
    const uint8_t *const taps[] = {
        above - 4, above + 4, mid - 8, mid + 8,
        fetch_bug ? mid - 8 : below - 4, fetch_bug ? mid + 8 : below + 4
    };
    sr_vi_extrema extrema = { center, center, center, center };
    for (unsigned i = 0; i < 6; i++) {
        const __m128i tap = sr_vi_load4(taps[i]);
        /* The centre already seeds both extrema, so it is neutral for
         * neighbours that lack full coverage. */
        const __m128i valid = sr_vi_select(sr_vi_full_coverage(tap), tap, center);
        sr_vi_aa_fold(&extrema, valid);
    }
    const __m128i zero = _mm_setzero_si128();
    __m128i coefficient = _mm_sub_epi32(_mm_set1_epi32(7), _mm_srli_epi32(center, 24));
    coefficient = _mm_or_si128(coefficient, _mm_slli_epi32(coefficient, 16));
    const __m128i lo = sr_vi_aa_adjust(_mm_unpacklo_epi8(center, zero),
        _mm_unpacklo_epi8(extrema.second_low, zero), _mm_unpacklo_epi8(extrema.second_high, zero),
        _mm_unpacklo_epi32(coefficient, coefficient));
    const __m128i hi = sr_vi_aa_adjust(_mm_unpackhi_epi8(center, zero),
        _mm_unpackhi_epi8(extrema.second_low, zero), _mm_unpackhi_epi8(extrema.second_high, zero),
        _mm_unpackhi_epi32(coefficient, coefficient));
    return _mm_packus_epi16(lo, hi);
}

static inline __m128i sr_vi_divot4(__m128i left, __m128i center, __m128i right)
{
    const __m128i full = sr_vi_full_coverage(_mm_and_si128(left, _mm_and_si128(center, right)));
    if (sr_all_mask32(full)) return center;
    const __m128i low = _mm_min_epu8(left, right);
    const __m128i high = _mm_max_epu8(left, right);
    const __m128i median = _mm_max_epu8(low, _mm_min_epu8(center, high));
    return sr_vi_select(full, center, median);
}

static inline __m128i sr_vi_dither4(__m128i rgba, __m128i noise)
{
    const __m128i r = _mm_and_si128(noise, _mm_set1_epi32(1));
    const __m128i g = _mm_slli_epi32(_mm_and_si128(noise, _mm_set1_epi32(2)), 7);
    const __m128i b = _mm_slli_epi32(_mm_and_si128(noise, _mm_set1_epi32(4)), 14);
    return _mm_adds_epu8(rgba, _mm_or_si128(r, _mm_or_si128(g,b)));
}

static inline __m128i sr_vi_dither_lo4(__m128i rgba, const uint16_t *noise)
{
    const __m128i packed = sr_vi_load8_bytes(noise);
    return sr_vi_dither4(rgba, _mm_unpacklo_epi16(packed, _mm_setzero_si128()));
}
#endif
#endif
