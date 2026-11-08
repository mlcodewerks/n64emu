/* MP3 synthesis handler clocks, including overlay load/restore and state-bank
 * transfers. Common task boot/command dispatch remains in the family estimate.
 * Profiles identify complete original programs, including the synthesis overlay.
 */
#ifndef HLE_TIMING_MP3_H
#define HLE_TIMING_MP3_H
#include "hle_timing_identity.h"

static uint32_t hle_timing_mp3_region(const uint32_t *program, const uint32_t *data,
                                    const uint32_t *current, uint32_t bank_bytes)
{
    uint32_t bytes, clocks;
    uint64_t signature;
    if (!program || !data || !current || data[0] || data[3] != 0x09bf1238u) return 0;
    if (bank_bytes == 0x440u && data[1] == 0x0f6f1080u && data[2] == 0xf70u && data[4] == 0x1ab0140cu) {
        bytes = 0x1930u; signature = UINT64_C(0x53906e0072527959); clocks = 21832u;
    } else if (bank_bytes == 0x440u && data[1] == 0x0f7f1080u && data[2] == 0xf80u && data[4] == 0x1ae8143cu) {
        bytes = 0x1940u; signature = UINT64_C(0x61555d4ded9488dc); clocks = 21834u;
    } else return 0;
    /* Reject descriptor writes by an earlier DSP command. Source-region
     * identity alone cannot certify a different overlay address or length. */
    if (current[1] != data[1] || current[2] != data[2] ||
        current[3] != data[3] || current[4] != data[4]) return 0;
    return hle_timing_word_fingerprint(program, bytes / 4u) == signature ? clocks : 0;
}
#endif
