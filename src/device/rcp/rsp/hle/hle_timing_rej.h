/* Compact Rej CULLDL, verified against the complete original text image. */
#ifndef HLE_TIMING_REJ_H
#define HLE_TIMING_REJ_H
#include "hle_timing_identity.h"

static inline int hle_timing_rej_profile(const uint32_t *text)
{
    /* The boot DMA loads 0xf80 bytes at IMEM +0x80. Exclude adjacent ROM. */
    return text && hle_timing_word_fingerprint(text, 0xf80u/4u)
        == UINT64_C(0x2252da9d3926efe4);
}

static inline uint32_t hle_timing_rej_cull(uint32_t visited, int culled, uint32_t depth)
{
    if (!visited || visited > 64u || (culled && !depth)) return 0;
    /* 0x18c..0x1b4 dispatch: 12. 0xc24..0xc48 handler:
     * first accepted record: 1+8*K; all rejected: 5+8*N.
     * A nested all-rejected return adds 8 at 0x1e4..0x1f4.
     * List DMA and the root termination path remain outside this region. */
    return (culled ? 25u : 13u) + 8u*visited;
}
#endif
