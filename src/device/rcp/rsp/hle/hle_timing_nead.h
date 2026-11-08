/* Native NEAD ADPCM loop increments relative to the zero-count path.
 * The count branch and initial/last header prefetch belong to this increment;
 * state DMA, common startup and command dispatch retain their existing model.
 * See the native ADPCM evidence under benchmarks/hle-calibration. */
#ifndef HLE_TIMING_NEAD_H
#define HLE_TIMING_NEAD_H
#include "hle_timing_identity.h"

static inline uint32_t hle_timing_nead_profile(const uint32_t *imem)
{
    uint64_t hash;
    /* Full loaded IMEM image, checked once when the task is seeded. Other
     * builds have separate common-cost gaps; their loop evidence alone
     * does not certify replacing a piece of their fitted whole-task model. */
    if (!imem) return 0;
    hash = hle_timing_word_fingerprint(imem, 1024u);
    if (hash == UINT64_C(0xd0be1d39ad245891)) return 1;
    if (hash == UINT64_C(0x1f584b205d5afc61)) return 2;
    if (hash == UINT64_C(0x945b97d076844d68)) return 3;
    if (hash == UINT64_C(0x2d179def6fa7b444)) return 4;
    if (hash == UINT64_C(0x562662df86c05f4c)) return 5;
    if (hash == UINT64_C(0xc781edabc413c129)) return 6;
    if (hash == UINT64_C(0x7a60ff0f71b76599)) return 7;
    return 0;
}

static inline uint32_t hle_timing_nead_adpcm(uint32_t frames, uint32_t flags,
                                           uint32_t special_lookahead_headers)
{
    if (!frames || frames > 127u || special_lookahead_headers > frames) return 0;
    return (flags & 4u ? 32u + 56u * frames : 33u + 57u * frames) + special_lookahead_headers;
}

/* Complete envelope handler, excluding common command dispatch. The four
 * xor masks do not branch; wet swap takes a four-clock detour per granule.
 * The signed counter is tested after each 16-sample iteration, even at zero. */
static inline uint32_t hle_timing_nead_envmix(uint32_t samples, uint32_t flags)
{
    uint32_t groups;
    if (samples > 255u) return 0;
    groups = samples ? (samples + 15u) / 16u : 1u;
    return 47u + groups * (42u + ((flags & 16u) ? 4u : 0u));
}

/* Full resample handler, including its state-DMA helpers. The flag paths
 * select initialization, history restore and the optional pre-loop setup.
 * Per-PC affine schedules cover all flags, pitch and phase fixtures. */
static inline uint32_t hle_timing_nead_resample(uint32_t profile, uint32_t groups, uint32_t flags)
{
    static const uint16_t base[7][8] = {
        {161,131,162,132,170,140,162,132},
        {185,155,186,156,194,164,186,156},
        {148,128,149,129,157,137,149,129},
        {153,126,154,127,162,135,154,127},
        {153,126,154,127,162,135,154,127},
        {148,128,149,129,157,137,149,129},
        {168,138,169,139,177,147,169,139}
    };
    if (!profile || profile > 7u || !groups || groups > 127u || flags > 7u) return 0;
    return base[profile - 1u][flags] + 44u * groups;
}
#endif
