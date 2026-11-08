/* Verified RE2 video instruction schedules; active work under the native RSP
 * pipeline/DMA model, excluding external CPU/RDP waits and bus contention.
 * The caller's C/SIMD branches are not charged as native instructions.
 */
#ifndef HLE_TIMING_VIDEO_H
#define HLE_TIMING_VIDEO_H
#include "hle_timing_identity.h"

enum hle_video_timing_kind { HLE_VIDEO_DECODE, HLE_VIDEO_FILL };
struct hle_video_timing_input {
    const uint32_t *program, *boot;
    uint32_t boot_bytes, flags, data_bytes, status, io_busy, unaligned;
    uint32_t kind, width, height, rows_per_dma, row_pairs, descriptor, stride_pair;
};

static uint32_t hle_timing_video(const struct hle_video_timing_input *in)
{
    uint32_t stride, skip, descriptor;
    uint64_t signature;
    if (!in || !in->program || !in->boot || in->kind > HLE_VIDEO_FILL ||
        in->boot_bytes != 0xd0u || (in->flags & 3u) || in->data_bytes != 128u ||
        in->io_busy || in->unaligned) return 0;
    if (hle_timing_word_fingerprint(in->boot, 0xd0u / 4u) != UINT64_C(0x0f7c8dfaf9f62fc0)) return 0;
    signature = hle_timing_word_fingerprint(in->program,
        in->kind == HLE_VIDEO_DECODE ? 0x368u / 4u : 0x148u / 4u);
    if (signature != (in->kind == HLE_VIDEO_DECODE ? UINT64_C(0x4242bd34337022ef)
                                                : UINT64_C(0x1082a720ada9cd9f))) return 0;
    if (in->status & 0x80u)
        return in->kind == HLE_VIDEO_FILL ? 61u : 0u;
    /* Two rows per native DMA; the inner loop consumes sixteen decode
     * pixels (eight-byte lanes in the fill). The wide-frame window path
     * reproduces overlap and wrapping of the fixed DMEM banks. */
    if (!in->width || (in->width & 15u) || in->width > 320u || !in->height ||
        (in->height & 1u) || in->rows_per_dma != 2u || in->row_pairs != in->height / 2u ||
        !in->stride_pair || (in->stride_pair & 15u)) return 0;
    stride = in->stride_pair / 2u;
    if (stride < in->width * 4u || in->height > 0x800000u / stride) return 0;
    skip = stride - in->width * 4u;
    if (skip > 0xff8u) return 0;
    descriptor = (skip << 20) | (in->width * 4u - 1u);
    if (in->kind == HLE_VIDEO_DECODE) descriptor |= 0x1000u;
    if (in->descriptor != descriptor) return 0;
    /* The entire task replaces the work model, including boot/setup and
     * initial/final iterations. No DMA/pixel cost is added a second time. */
    if (in->kind == HLE_VIDEO_DECODE)
        return 833u + in->row_pairs * (133u + 22u * in->width);
    return 602u + in->row_pairs * (98u + 28u * in->width);
}
#endif
