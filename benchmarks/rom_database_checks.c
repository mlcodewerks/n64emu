#include <assert.h>
#include <stdio.h>
#include <utils/md5.h>

#include "main/rom_database.h"

static uint8_t boot_digest[16];
static size_t hashed_size;

/* Inject a known boot fingerprint to test lookup priority without a ROM. */
void MD5_Init(MD5_CTX *state)
{
    (void)state;
    hashed_size = 0;
}

void MD5_Update(MD5_CTX *state, const void *data, unsigned long size)
{
    (void)state;
    (void)data;
    hashed_size += size;
}

void MD5_Final(unsigned char *digest, MD5_CTX *state)
{
    (void)state;
    memcpy(digest, boot_digest, sizeof(boot_digest));
}

int main(void)
{
    static const uint8_t kart_md5[16] = {
        0x3a, 0x67, 0xd9, 0x98, 0x6f, 0x54, 0xeb, 0x28,
        0x29, 0x24, 0xfc, 0xa4, 0xcd, 0x5f, 0x6d, 0xff,
    };
    static const uint8_t pal_md5[16] = {
        0x75, 0xcd, 0x95, 0xd3, 0x38, 0xf7, 0x2d, 0x79,
        0x46, 0x76, 0x8c, 0x32, 0xbc, 0x87, 0x95, 0x8d,
    };
    static const uint8_t unknown_md5[16] = {0};
    uint8_t rom[8192] = {0};
    core_rom_settings settings = {0};
    core_system_type region = SYSTEM_NTSC;
    memcpy(rom + 0x3b, "NDOE", 4);

    rom_database_mister(rom, sizeof(rom), unknown_md5, &settings, &region);
    assert(hashed_size == 4096 && settings.savetype == SAVETYPE_EEPROM_16K && settings.rumble);

    rom_database_mister(rom, sizeof(rom), kart_md5, &settings, &region);
    assert(settings.savetype == SAVETYPE_EEPROM_4K && settings.mempak && !settings.rumble);

    memcpy(boot_digest, pal_md5, sizeof(boot_digest));
    rom_database_mister(rom, sizeof(rom), kart_md5, &settings, &region);
    assert(region == SYSTEM_PAL && settings.savetype == SAVETYPE_NONE && settings.mempak && settings.rumble);

    memset(boot_digest, 0, sizeof(boot_digest));
    memcpy(rom + 0x3b, "NWRE", 4);
    rom[0x3f] = 2;
    rom_database_mister(rom, sizeof(rom), unknown_md5, &settings, &region);
    assert(settings.savetype == SAVETYPE_EEPROM_4K && settings.mempak && settings.rumble);
    rom[0x3f] = 1;
    rom_database_mister(rom, sizeof(rom), unknown_md5, &settings, &region);
    assert(settings.savetype == SAVETYPE_EEPROM_4K && settings.mempak && !settings.rumble);

    memcpy(rom + 0x3b, "NEDE", 4);
    rom[0x3f] = 0x50;
    rom_database_settings(rom, &settings);
    rom_database_mister(rom, sizeof(rom), kart_md5, &settings, &region);
    assert(settings.savetype == SAVETYPE_FLASH_RAM && !settings.mempak);

    memcpy(rom + 0x3b, "BPYE", 4);
    rom_database_settings(rom, &settings);
    rom_database_mister(rom, sizeof(rom), unknown_md5, &settings, &region);
    assert(settings.savetype == SAVETYPE_EEPROM_4K);
    puts("ROM database hash precedence, ID wildcards, regions and fallbacks passed.");
    return 0;
}
