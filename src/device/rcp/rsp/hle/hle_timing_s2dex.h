/* Original S2DEX 1.07 resident/startup/BG text and dispatch data. */
#ifndef HLE_TIMING_S2DEX_H
#define HLE_TIMING_S2DEX_H
#include "hle_timing_identity.h"

static inline uint32_t hle_timing_s2dex_profile(const uint32_t *text,const uint32_t *data)
{
    return text && data
        && hle_timing_word_fingerprint(text,0x17e8u/4u)==UINT64_C(0x86545354feb6f013)
        && hle_timing_word_fingerprint(data,0x3b8u/4u)==UINT64_C(0xfe3aba8a0d346e8a);
}

/* Fixed command paths, including normal dispatch but excluding FIFO/list
 * transfers. Other-mode lookahead emits zero or one additional 11-clock
 * packet path. TEXRECT's two scalar HALF commands are covered by its caller. */
static inline uint32_t hle_timing_s2dex_command(uint32_t w0,int32_t *packet_cost)
{
    *packet_cost=0;
    switch(w0>>24) {
    case 0xb6:case 0xb7:return 12;
    case 0xb1:return 29;
    case 0xbc:return (w0&0xffu)==6u && ((w0>>8)&0xffffu)<64u ? 20u : 0u;
    case 0xb9:*packet_cost=11;return 29;
    case 0xba:*packet_cost=11;return 30;
    case 0xe6:case 0xe7:case 0xe9:return 22;
    case 0xed:case 0xfd:case 0xff:return 33;
    case 0xef:return 25;
    case 0xf2:case 0xf4:case 0xf5:case 0xf9:
    case 0xfa:case 0xfb:case 0xfc:return 21;
    default:return 0;
    }
}
#endif
