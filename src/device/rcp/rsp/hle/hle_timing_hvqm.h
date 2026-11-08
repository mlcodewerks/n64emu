/* Original HVQM2sp1/sp2 instruction schedules. See HVQM_TIMING.md.
 * Active clocks include the repository RSP issue, redirect and DMA model;
 * external bus contention and asynchronous yield/resume are not covered. */
#ifndef HLE_TIMING_HVQM_H
#define HLE_TIMING_HVQM_H
#include "hle_timing_identity.h"

struct hle_hvqm_timing {
    uint32_t valid, clocks, consumed;
};

static inline struct hle_hvqm_timing hle_timing_hvqm_begin(
    const uint32_t *program, const uint32_t *data, const uint32_t *boot,
    uint32_t code_bytes, uint32_t data_bytes, uint32_t boot_bytes,
    uint32_t flags, uint32_t status, uint32_t io_busy, uint32_t unaligned,
    uint32_t rows, int is32)
{
    struct hle_hvqm_timing out = {0, 0, 0};
    uint32_t text = is32 ? 0x5e0u : 0x5d0u;
    if (!program || !data || !boot || flags || io_busy || unaligned ||
        (status & 0x80u) || !rows || code_bytes != text || boot_bytes != 0xd0u ||
        (data_bytes != 0x70u && data_bytes != (is32 ? 0xdd0u : 0xd50u))) return out;
    if (hle_timing_word_fingerprint(program, text / 4u) !=
        (is32 ? UINT64_C(0x30cd84e1752af6ea) : UINT64_C(0xa1c99c5900a9c7a2)) ||
        hle_timing_word_fingerprint(data, 0x70u / 4u) !=
        (is32 ? UINT64_C(0x50fc1ca179312060) : UINT64_C(0x47ff4a911af8ba60)) ||
        hle_timing_word_fingerprint(boot, 0xd0u / 4u) !=
        UINT64_C(0x0f7c8dfaf9f62fc0)) return out;
    out.valid = 1;
    out.clocks = 1050u + data_bytes / 8u + 4u * rows;
    return out;
}

static inline void hle_timing_hvqm_take(struct hle_hvqm_timing *t, int basis)
{
    if (t->consumed && !(t->consumed & 127u))
        t->clocks += basis ? 31u : 30u;
    t->consumed += 8u;
}

static inline void hle_timing_hvqm_block(struct hle_hvqm_timing *t,
                                       uint8_t block, uint8_t prior_base)
{
    uint32_t bases = block & 7u;
    if (!t->valid) return;
    hle_timing_hvqm_take(t, 0);
    if (block & prior_base) {
        t->clocks += 20u;
    } else if (!block) {
        t->clocks += 49u;
    } else if (!(block & 15u) || !bases) {
        t->clocks += !(block & 15u) ? 53u : 60u;
        hle_timing_hvqm_take(t, 0);
        hle_timing_hvqm_take(t, 0);
    } else {
        t->clocks += 137u + (bases - 1u) * 106u;
        while (bases--) hle_timing_hvqm_take(t, 1);
        if (block & 8u) {
            t->clocks += 29u;
            hle_timing_hvqm_take(t, 0);
            hle_timing_hvqm_take(t, 0);
        }
    }
}

static inline void hle_timing_hvqm_mcu(struct hle_hvqm_timing *t,
                                     uint32_t vertical, int is32, int complete)
{
    if (t->valid) {
        uint32_t active = (vertical == 1u ? 345u : 559u) - (is32 ? 16u * vertical : 0u);
        t->clocks += complete ? active - 20u : 20u;
    }
}
#endif
