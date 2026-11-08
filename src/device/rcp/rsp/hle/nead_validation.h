#ifndef N64EMU_NEAD_VALIDATION_H
#define N64EMU_NEAD_VALIDATION_H
#include <stdint.h>
#include <string.h>
#include "hle_timing_nead.h"

/* An IMEM LOAD changes the program that will execute next. A zero masked
 * length can encode 256 DMA rows and overwrite command/control memory.
 * Select native execution before HLE changes any task state. The two proved
 * zero-return programs can retain HLE after complete executable validation. */
static inline int nead_load_hle_supported(const uint32_t task[16],
    const uint8_t *ram, uint32_t ram_size, uint32_t signature)
{
    uint32_t list = task[12] & 0x7fffffu, bytes = task[13], i;
    uint32_t verified_early = 0;
    switch (signature) {
    case 0x11181350u: case 0x111812e0u: case 0x110412acu: case 0x110412ccu:
    case 0x1cd01250u: case 0x1f08122cu: case 0x1f38122cu: case 0x1f681230u:
    case 0x1f801250u: case 0x109411f8u: case 0x1eac11b8u: case 0x1f701238u:
    case 0x1f4c1230u: break;
    default: return 1;
    }
    if (!bytes || (bytes & 7u) || (list & 7u) || bytes > ram_size ||
        list > ram_size - bytes) return 0;
    for (i = 0; i < bytes; i += 8u) {
        uint32_t command;
        memcpy(&command, ram + list + i, 4);
        if (((command >> 24) & 0x7fu) != 0x14u) continue;
        if (!((command >> 12) & 0xff0u) &&
            (signature == 0x1eac11b8u || signature == 0x109411f8u)) {
            if (!verified_early) {
                uint32_t image[1024], text = task[4] & 0x7fffffu;
                if (text > ram_size - sizeof(image) || (text & 3u)) return 0;
                memcpy(image, ram + text, sizeof(image));
                verified_early = hle_timing_nead_profile(image);
                if (verified_early != (signature == 0x1eac11b8u ? 3u : 6u)) return 0;
            }
            continue;
        }
        /* Mario Kart adds its 0x450 audio-buffer base before SP_MEM_ADDR
         * selects DMEM/IMEM. A carry across 0x1000 changes the bank. */
        if (!((command >> 12) & 0xff0u) ||
            (((command & 0xffffu) + (signature == 0x11181350u ? 0x450u : 0u)) & 0x1000u)) return 0;
    }
    return 1;
}
#endif
