#ifndef HLE_TIMING_IDENTITY_H
#define HLE_TIMING_IDENTITY_H
#include <stdint.h>

/* Fingerprint architectural words, independent of host byte order. Adjacent
 * ROM data must not be included when identifying an executable region. */
static inline uint64_t hle_timing_word_fingerprint(const uint32_t *words, uint32_t count)
{
    uint64_t hash = UINT64_C(14695981039346656037);
    uint32_t i;
    for (i = 0; i < count; ++i)
        hash = (hash ^ words[i]) * UINT64_C(1099511628211);
    return hash;
}
#endif
