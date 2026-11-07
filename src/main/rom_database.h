#ifndef N64EMU_ROM_DATABASE_H
#define N64EMU_ROM_DATABASE_H

#include "rom_database_ares.h"

/* Local board variants and homebrew additions stay outside the generated
 * upstream database so regenerating it does not remove compatibility fixes. */
static inline void rom_database_settings(const uint8_t header[64], core_rom_settings *settings)
{
    ares_rom_settings(header, settings);
    if (header[0x3b] == 'C' && settings->savetype == SAVETYPE_NONE) {
        uint8_t original[64];
        core_rom_settings fallback = *settings;
        memcpy(original, header, sizeof(original));
        original[0x3b] = 'N';
        ares_rom_settings(original, &fallback);
        /* Cartridge conversions/translations can retain the original game's
         * board ID with a C prefix (CD6 and CPY). Explicit C-board records
         * and the EverDrive header retain their original settings. */
        if (fallback.savetype != SAVETYPE_NONE) *settings = fallback;
    }
    /* Pyoro's original NTSC/PAL releases predate the EverDrive header. */
    if (memcmp(header + 0x3b, "BPY", 3) == 0)
        settings->savetype = SAVETYPE_EEPROM_4K;
}

#endif
