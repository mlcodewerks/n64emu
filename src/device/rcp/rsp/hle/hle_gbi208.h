/* Complete resident identities for the two audited FIFO 2.08 builds.
 * Matrix defaults are data, not identity constants: seed the actual DMA image. */
#ifndef HLE_GBI208_H
#define HLE_GBI208_H
#include "hle_timing_identity.h"
static inline unsigned hle_gbi208_profile(const uint32_t *text)
{
    uint64_t hash;
    if(!text)return 0;
    hash=hle_timing_word_fingerprint(text,0xf80u/4u);
    if(hash==UINT64_C(0xac310503c83b23ea))return 1;
    if(hash==UINT64_C(0xdee3d09f275847b0))return 2;
    return 0;
}
#endif
