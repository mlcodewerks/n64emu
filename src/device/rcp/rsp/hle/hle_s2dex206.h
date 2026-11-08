/* Original S2DEX FIFO 2.06 resident, startup and both overlays, plus data. */
#ifndef HLE_S2DEX206_H
#define HLE_S2DEX206_H
#include "hle_timing_identity.h"
static inline int hle_s2dex206_profile(const uint32_t *text,const uint32_t *data)
{
    return text && data &&
        hle_timing_word_fingerprint(text,0x18c0u/4u)==UINT64_C(0xab70116767f8cdaf) &&
        hle_timing_word_fingerprint(data,0x390u/4u)==UINT64_C(0xf5114c93c1d8c7c6);
}
#endif
