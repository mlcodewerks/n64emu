/* Instruction-derived active clocks for the verified PS JPEG program.
 * Native traces include pipeline interlocks, dual issue, redirects and DMA.
 * External bus contention and CPU/RDP waits are outside this estimate.
 * See benchmarks/hle-calibration/JPEG_TIMING.md for the executable evidence.
 */
#ifndef HLE_TIMING_JPEG_H
#define HLE_TIMING_JPEG_H
#include "hle_timing_identity.h"

struct hle_jpeg_ps_timing_input {
    const uint32_t *program;
    const uint32_t *boot;
    uint32_t boot_bytes, flags, ucode_data_bytes, parameter_bytes;
    uint32_t status, io_busy, unaligned;
    uint32_t macroblocks, mode;
};

/* Post-block SIG0 path relative to completion at that same block: native
 * counters add 63 issue/interlock clocks, four redirects, a 512-byte context
 * DMA, and the next input block's prefetch DMA. Verified in both PS loaders
 * and RGB16, modes 0/2. PS0 adds two issue clocks on this branch. Original
 * PC traces and matched completion/yield counter differences are retained. */
static uint32_t hle_timing_jpeg_yield_delta(uint32_t stride, int ps0)
{
    return 67u + (ps0 ? 2u : 0u) + 64u + stride / 8u;
}

static uint32_t hle_timing_jpeg_ps(const struct hle_jpeg_ps_timing_input *in)
{
    uint32_t base;
    uint64_t boot;
    int ps0;
    if (!in || !in->program || !in->boot || (in->flags & ~1u) || in->io_busy ||
        in->unaligned || (in->status & 0x80u) ||
        in->ucode_data_bytes != ((in->flags & 1u) ? 0x200u : 0x800u) ||
        (!(in->flags & 1u) && in->parameter_bytes != 32u) || !in->macroblocks ||
        (in->mode != 0 && in->mode != 2) ||
        in->macroblocks > 0x800000u / ((in->mode + 4u) * 128u)) return 0;
    if (in->boot_bytes != 0xd0u && in->boot_bytes != 0x160u) return 0;
    ps0 = hle_timing_word_fingerprint(in->program, 0x8e0u / 4u) ==
        UINT64_C(0x246750651bd72e1e);
    if (!ps0 && hle_timing_word_fingerprint(in->program, 0xaf0u / 4u) !=
        UINT64_C(0x42c9b4072d213afe)) return 0;
    boot = hle_timing_word_fingerprint(in->boot, in->boot_bytes / 4u);
    /* The restored path bypasses parameter/quantizer DMA and first-block
     * setup. Its original-PC startup/termination sum is 669 clocks. */
    base = (in->flags & 1u) ? 669u : (in->mode ? 1049u : 1050u);
    if (in->boot_bytes == 0xd0u && boot == UINT64_C(0x0f7c8dfaf9f62fc0)) {
        base += 8u;
    } else if (in->boot_bytes != 0x160u || boot != UINT64_C(0xc4de5b1f29d3b01d) ||
               !(in->status & 0x4000u)) {
        /* The extended loader has other self-test and DPC_CLOCK branches.
         * Only its already-patched entry with SIG7 set is covered here. */
        return 0;
    }
    /* Fixed loop schedule: first/last prefetch paths and final transfers are
     * included. This replaces the entire task estimate, avoiding a second
     * charge for the old image-block and DMA coefficients. */
    /* PS0 loop regions: mode 0 = 1321 issue + 96 DMA + 26 redirect;
     * mode 2 = 1858 issue + 160 DMA + 32 redirect. Startup/termination
     * contribute the same fixed base as PS, including native resumes. */
    return base + in->macroblocks * (ps0 ? (in->mode ? 2050u : 1443u) :
                                                        (in->mode ? 2555u : 1657u));
}

struct hle_jpeg_ob_timing_input {
    const uint32_t *program;
    const uint32_t *boot;
    uint32_t boot_bytes, flags, ucode_data_bytes;
    uint32_t status, io_busy, unaligned, macroblocks;
    int32_t scale;
};

static uint32_t hle_timing_jpeg_ob(const struct hle_jpeg_ob_timing_input *in)
{
    if (!in || !in->program || !in->boot || in->io_busy ||
        in->unaligned || (in->status & 0x80u) || in->boot_bytes != 0xd0u ||
        in->ucode_data_bytes != 0x800u || !in->macroblocks ||
        in->macroblocks > 0x800000u / 768u || in->scale < -31) return 0;
    if (hle_timing_word_fingerprint(in->program, 0x7b4u / 4u) !=
        UINT64_C(0x590aa3b2467065f9) ||
        hle_timing_word_fingerprint(in->boot, 0xd0u / 4u) !=
        UINT64_C(0x0f7c8dfaf9f62fc0)) return 0;
    /* Native quantizer sign branches run for every macroblock. Host qtable
     * construction is outside that loop and cannot supply these costs. */
    return 863u + in->macroblocks *
        (in->scale == 0 ? 1752u : in->scale > 0 ? 1754u : 1758u);
}
#endif
