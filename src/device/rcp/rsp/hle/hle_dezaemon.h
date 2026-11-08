/* Original Dezaemon 3D resident: the writer keeps its shade section
 * when G_SHADE is clear. Admission requires every resident word. */
#ifndef HLE_DEZAEMON_H
#define HLE_DEZAEMON_H
#include "hle_timing_identity.h"
static inline unsigned hle_dezaemon_profile(const uint32_t *text)
{
    return text && hle_timing_word_fingerprint(text,0xf80u/4u)==UINT64_C(0xd267a5091517b407);
}
#endif
