#include "rom_database.h"
#include "rom_database_mister.h"

#include <ctype.h>
#include <utils/md5.h>

static const uint8_t *find_mister_md5(const uint8_t md5[16])
{
    size_t lo = 0, hi = sizeof(mister_md5_entries) / sizeof(mister_md5_entries[0]);
    while (lo < hi) {
        size_t mid = lo + (hi - lo) / 2;
        int order = memcmp(md5, mister_md5_entries[mid].md5, 16);
        if (order == 0) return &mister_md5_entries[mid].settings;
        if (order < 0) hi = mid;
        else lo = mid + 1;
    }
    return NULL;
}

static const uint8_t *find_mister_cart_id(const uint8_t *rom)
{
    static const char hex[] = "0123456789ABCDEF";
    char id[6];
    for (size_t i = 0; i < 4; ++i) {
        if (!isalnum(rom[0x3b + i])) return NULL;
        id[i] = rom[0x3b + i];
    }
    id[4] = hex[rom[0x3f] >> 4];
    id[5] = hex[rom[0x3f] & 15];

    /* Keep upstream order. Region/revision rules precede their wildcards. */
    for (size_t i = 0; i < sizeof(mister_cart_entries) / sizeof(mister_cart_entries[0]); ++i) {
        const char *pattern = mister_cart_entries[i].pattern;
        size_t j = 0;
        while (j < sizeof(id) && (pattern[j] == '_' || pattern[j] == id[j]))
            ++j;
        if (j == sizeof(id)) return &mister_cart_entries[i].settings;
    }
    return NULL;
}

void rom_database_mister(const uint8_t *rom, size_t size, const uint8_t rom_md5[16],
    core_rom_settings *settings, core_system_type *region)
{
    /* The extended homebrew header explicitly configures its hardware. */
    if (size < sizeof(core_rom_header) || (rom[0x3c] == 'E' && rom[0x3d] == 'D'))
        return;

    /* MiSTer fingerprints the first 4 KiB (header and IPL3) separately. */
    MD5_CTX hash;
    uint8_t boot_md5[16];
    MD5_Init(&hash);
    MD5_Update(&hash, rom, size < 4096 ? size : 4096);
    MD5_Final(boot_md5, &hash);

    const uint8_t *entry = find_mister_md5(boot_md5);
    if (!entry) entry = find_mister_md5(rom_md5);
    if (!entry) entry = find_mister_cart_id(rom);
    if (!entry) return;

    uint8_t value = *entry;
    settings->savetype = value & MISTER_SAVE_MASK;
    settings->mempak = (value & MISTER_MEMPAK) != 0;
    settings->rumble = (value & MISTER_RUMBLE) != 0;
    settings->transferpak = (value & MISTER_TRANSFERPAK) != 0;
    if (value & MISTER_NTSC) *region = SYSTEM_NTSC;
    else if (value & MISTER_PAL) *region = SYSTEM_PAL;
}
