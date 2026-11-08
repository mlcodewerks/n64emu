/* Original RGB16 JPEG executable/data; output bit zero is cleared. */
#ifndef HLE_JPEG_RGB16_H
#define HLE_JPEG_RGB16_H
#include "hle_timing_identity.h"
struct hle_t;
void jpeg_decode_RGB16(struct hle_t *hle);
static inline int hle_jpeg_rgb16_profile(const uint32_t *text,const uint32_t *data)
{
    return text && data &&
        hle_timing_word_fingerprint(text,0xad0u/4u)==UINT64_C(0x32449fb0b8814a54) &&
        hle_timing_word_fingerprint(data,0x60u/4u)==UINT64_C(0x9cfec85026fe7311);
}
#include "hle_timing_jpeg.h"
/* Original fresh/resumed task schedules. Counts come from the own PC/opcode traces,
 * including boot, constant/quantizer reads, pipelined first/last blocks and
 * final write. External semaphore/DMA waits are excluded. */
static inline uint32_t hle_timing_jpeg_rgb16(const struct hle_jpeg_ps_timing_input *in,
                                          const uint32_t *data)
{
    if(!in || !in->program || !in->boot || in->io_busy ||
       in->unaligned || (in->status&0x80u) || in->boot_bytes!=0xd0u ||
       in->ucode_data_bytes!=((in->flags&1u)?0x200u:0x60u) || (!(in->flags&1u) && in->parameter_bytes!=32u) ||
       !in->macroblocks || (in->mode!=0 && in->mode!=2) ||
       in->macroblocks>0x800000u/((in->mode+4u)*128u) ||
       !hle_jpeg_rgb16_profile(in->program,data) ||
       hle_timing_word_fingerprint(in->boot,0xd0u/4u)!=UINT64_C(0x0f7c8dfaf9f62fc0))return 0;
    return ((in->flags&1u)?677u:(in->mode ? 813u : 814u))+in->macroblocks*(in->mode ? 2519u : 1641u);
}
#endif
