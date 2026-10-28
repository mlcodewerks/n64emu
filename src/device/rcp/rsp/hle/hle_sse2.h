/* Packed integer building blocks shared by the SSE2 and NEON HLE paths.
 * Keep the signed products and byte-mask selection semantics exact: the
 * RSP's 48-bit accumulators observe bits that ordinary saturated DSP drops. */
#ifndef PARALLAX_HLE_SSE2_H
#define PARALLAX_HLE_SSE2_H

static inline __m128i hle_select_bits(__m128i mask, __m128i yes, __m128i no)
{
    return _mm_or_si128(_mm_and_si128(mask, yes), _mm_andnot_si128(mask, no));
}

static inline __m128i hle_cmpgt64(__m128i a, __m128i b)
{
    __m128i sign = _mm_set1_epi32((int32_t)0x80000000u);
    __m128i hi = _mm_shuffle_epi32(_mm_cmpgt_epi32(a, b), _MM_SHUFFLE(3,3,1,1));
    __m128i eq = _mm_shuffle_epi32(_mm_cmpeq_epi32(a, b), _MM_SHUFFLE(3,3,1,1));
    __m128i lo = _mm_cmpgt_epi32(_mm_xor_si128(a, sign), _mm_xor_si128(b, sign));
    lo = _mm_shuffle_epi32(lo, _MM_SHUFFLE(2,2,0,0));
    return _mm_or_si128(hi, _mm_and_si128(eq, lo));
}

static inline __m128i hle_sext16_32(__m128i a)
{
    return _mm_unpacklo_epi16(a, _mm_srai_epi16(a, 15));
}

static inline __m128i hle_zext16_32(__m128i a)
{
    return _mm_unpacklo_epi16(a, _mm_setzero_si128());
}

static inline __m128i hle_sext32_64(__m128i a)
{
    return _mm_unpacklo_epi32(a, _mm_srai_epi32(a, 31));
}

static inline __m128i hle_mullo32(__m128i a, __m128i b)
{
    __m128i even = _mm_mul_epu32(a, b);
    __m128i odd = _mm_mul_epu32(_mm_srli_si128(a, 4), _mm_srli_si128(b, 4));
    even = _mm_shuffle_epi32(even, _MM_SHUFFLE(2,0,2,0));
    odd = _mm_shuffle_epi32(odd, _MM_SHUFFLE(2,0,2,0));
    return _mm_unpacklo_epi32(even, odd);
}

/* Signed products of dwords 0 and 2. PMULUDQ's two unsigned products
 * differ by exactly 2^32 times the opposite operand for each sign bit. */
static inline __m128i hle_mul_s32(__m128i a, __m128i b)
{
    __m128i product = _mm_mul_epu32(a, b);
    __m128i sa = _mm_and_si128(_mm_srai_epi32(a, 31), b);
    __m128i sb = _mm_and_si128(_mm_srai_epi32(b, 31), a);
    product = _mm_sub_epi64(product, _mm_slli_epi64(sa, 32));
    return _mm_sub_epi64(product, _mm_slli_epi64(sb, 32));
}

static inline __m128i hle_min32(__m128i a, __m128i b)
{
    return hle_select_bits(_mm_cmpgt_epi32(a, b), b, a);
}

static inline __m128i hle_max32(__m128i a, __m128i b)
{
    return hle_select_bits(_mm_cmpgt_epi32(a, b), a, b);
}

static inline __m128i hle_minu32(__m128i a, __m128i b)
{
    __m128i sign = _mm_set1_epi32((int32_t)0x80000000u);
    return hle_select_bits(_mm_cmpgt_epi32(_mm_xor_si128(a, sign), _mm_xor_si128(b, sign)), b, a);
}

static inline __m128i hle_hadds16(__m128i a, __m128i b)
{
    __m128i ones = _mm_set1_epi16(1);
    return _mm_packs_epi32(_mm_madd_epi16(a, ones), _mm_madd_epi16(b, ones));
}

static inline __m128i hle_reverse16(__m128i a)
{
    a = _mm_shufflelo_epi16(a, _MM_SHUFFLE(0,1,2,3));
    a = _mm_shufflehi_epi16(a, _MM_SHUFFLE(0,1,2,3));
    return _mm_shuffle_epi32(a, _MM_SHUFFLE(1,0,3,2));
}

/* The RSP's element selectors are halfword broadcasts, not arbitrary byte
 * permutations. Immediate shuffles avoid a scalar lane loop and a PSHUFB
 * requirement, including the two independent four-lane broadcasts. */
static inline __m128i hle_select_element(__m128i a, unsigned e)
{
#define HLE_SELECT_QUAD(n) _mm_shufflehi_epi16(_mm_shufflelo_epi16(a, _MM_SHUFFLE(n,n,n,n)), _MM_SHUFFLE(n,n,n,n))
#define HLE_SELECT_ALL(n) _mm_shuffle_epi32(HLE_SELECT_QUAD(n), _MM_SHUFFLE(0,0,0,0))
    switch (e & 15u) {
    case 0: case 1: return a;
    case 2: return _mm_shufflehi_epi16(_mm_shufflelo_epi16(a, _MM_SHUFFLE(2,2,0,0)), _MM_SHUFFLE(2,2,0,0));
    case 3: return _mm_shufflehi_epi16(_mm_shufflelo_epi16(a, _MM_SHUFFLE(3,3,1,1)), _MM_SHUFFLE(3,3,1,1));
    case 4: return HLE_SELECT_QUAD(0);
    case 5: return HLE_SELECT_QUAD(1);
    case 6: return HLE_SELECT_QUAD(2);
    case 7: return HLE_SELECT_QUAD(3);
    case 8: return HLE_SELECT_ALL(0);
    case 9: return HLE_SELECT_ALL(1);
    case 10: return HLE_SELECT_ALL(2);
    case 11: return HLE_SELECT_ALL(3);
    default:
        a = _mm_srli_si128(a, 8);
        switch (e & 3u) {
        case 0: return HLE_SELECT_ALL(0);
        case 1: return HLE_SELECT_ALL(1);
        case 2: return HLE_SELECT_ALL(2);
        default: return HLE_SELECT_ALL(3);
        }
    }
#undef HLE_SELECT_ALL
#undef HLE_SELECT_QUAD
}
#endif
