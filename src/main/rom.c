

#include <ctype.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define __STDC_FORMAT_MACROS
#include <inttypes.h>

#define N64EMU_CORE_PROTOTYPES 1
#include "core/log.h"

#include "rom_database.h"
#include "core/types.h"
#include "device/device.h"
#include "device/r4300/jit_runtime.h"
#include "main.h"
#include <utils/md5.h>
#include "osal/preproc.h"
#include "rom.h"
#include "util.h"

#define CHUNKSIZE 1024*128 /* Read files 128KB at a time. */

/* Global loaded rom size. */
int g_rom_size = 0;

core_rom_header   ROM_HEADER;
rom_params        ROM_PARAMS;
core_rom_settings ROM_SETTINGS;

static core_system_type rom_country_code_to_system_type(uint16_t country_code);

static const uint8_t Z64_SIGNATURE[4] = { 0x80, 0x37, 0x12, 0x40 };
static const uint8_t V64_SIGNATURE[4] = { 0x37, 0x80, 0x40, 0x12 };
static const uint8_t N64_SIGNATURE[4] = { 0x40, 0x12, 0x37, 0x80 };
/* The DD subsystem loads the IPL as content and also maps it in DD ROM. */
static const uint8_t DD_Z64_SIGNATURE[4] = { 0x80, 0x27, 0x07, 0x40 };
static const uint8_t DD_V64_SIGNATURE[4] = { 0x27, 0x80, 0x40, 0x07 };
static const uint8_t DD_N64_SIGNATURE[4] = { 0x40, 0x07, 0x27, 0x80 };

/* Tests if a file is a valid N64 rom by checking the first 4 bytes. */
static int is_valid_rom(const unsigned char *buffer)
{
    if (memcmp(buffer, Z64_SIGNATURE, sizeof(Z64_SIGNATURE)) == 0
     || memcmp(buffer, V64_SIGNATURE, sizeof(V64_SIGNATURE)) == 0
     || memcmp(buffer, N64_SIGNATURE, sizeof(N64_SIGNATURE)) == 0
     || memcmp(buffer, DD_Z64_SIGNATURE, sizeof(DD_Z64_SIGNATURE)) == 0
     || memcmp(buffer, DD_V64_SIGNATURE, sizeof(DD_V64_SIGNATURE)) == 0
     || memcmp(buffer, DD_N64_SIGNATURE, sizeof(DD_N64_SIGNATURE)) == 0)
        return 1;
    else
        return 0;
}

/* Copies the source block of memory to the destination block of memory while
 * switching the endianness of .v64 and .n64 images to the .z64 format, which
 * is native to the Nintendo 64. The data extraction routines and MD5 hashing
 * function may only act on the .z64 big-endian format.
 *
 * IN: src: The source block of memory. This must be a valid Nintendo 64 ROM
 *          image of 'len' bytes.
 *     len: The length of the source and destination, in bytes.
 * OUT: dst: The destination block of memory. This must be a valid buffer for
 *           at least 'len' bytes.
 *      imagetype: A pointer to a byte that gets updated with the value of
 *                 V64IMAGE, N64IMAGE or Z64IMAGE according to the format of
 *                 the source block. The value is undefined if 'src' does not
 *                 represent a valid Nintendo 64 ROM image.
 */
static void swap_copy_rom(void* dst, const void* src, size_t len, unsigned char* imagetype)
{
    if (memcmp(src, V64_SIGNATURE, sizeof(V64_SIGNATURE)) == 0 ||
        memcmp(src, DD_V64_SIGNATURE, sizeof(DD_V64_SIGNATURE)) == 0)
    {
        size_t i;
        const uint16_t* src16 = (const uint16_t*) src;
        uint16_t* dst16 = (uint16_t*) dst;

        *imagetype = V64IMAGE;
        /* .v64 images have byte-swapped half-words (16-bit). */
        for (i = 0; i < len; i += 2)
        {
            *dst16++ = core_swap16(*src16++);
        }
    }
    else if (memcmp(src, N64_SIGNATURE, sizeof(N64_SIGNATURE)) == 0 ||
        memcmp(src, DD_N64_SIGNATURE, sizeof(DD_N64_SIGNATURE)) == 0)
    {
        size_t i;
        const uint32_t* src32 = (const uint32_t*) src;
        uint32_t* dst32 = (uint32_t*) dst;

        *imagetype = N64IMAGE;
        /* .n64 images have byte-swapped words (32-bit). */
        for (i = 0; i < len; i += 4)
        {
            *dst32++ = core_swap32(*src32++);
        }
    }
    else {
        *imagetype = Z64IMAGE;
        memcpy(dst, src, len);
    }
}

core_result open_rom(const unsigned char* romimage, unsigned int size)
{
    MD5_CTX state;
    unsigned char digest[16];
    char buffer[256];
    unsigned char imagetype;
    int i;

    /* check input requirements */
    if (romimage == NULL || size < sizeof(core_rom_header) || !is_valid_rom(romimage))
    {
        core_log(CORE_LOG_ERROR, "open_rom(): not a valid ROM image");
        return CORE_INVALID_INPUT;
    }

    /* Clear Byte-swapped flag, since ROM is now deleted. */
    g_RomWordsLittleEndian = 0;
    /* allocate new buffer for ROM and copy into this buffer */
    g_rom_size = size;
    swap_copy_rom((uint8_t*)mem_base_u32(g_mem_base, MM_CART_ROM), romimage, size, &imagetype);
    /* ROM is now in N64 native (big endian) byte order */

    memcpy(&ROM_HEADER, (uint8_t*)mem_base_u32(g_mem_base, MM_CART_ROM), sizeof(core_rom_header));

    /* Calculate MD5 hash  */
    MD5_Init(&state);
    MD5_Update(&state, mem_base_u32(g_mem_base, MM_CART_ROM), g_rom_size);
    MD5_Final(digest, &state);
    for ( i = 0; i < 16; ++i )
        sprintf(buffer+i*2, "%02X", digest[i]);
    buffer[32] = '\0';
    strcpy(ROM_SETTINGS.MD5, buffer);

    /* add some useful properties to ROM_PARAMS */
    ROM_PARAMS.systemtype = rom_country_code_to_system_type(((const uint8_t*)&ROM_HEADER)[0x3e]);

    memcpy(ROM_PARAMS.headername, ROM_HEADER.Name, 20);
    ROM_PARAMS.headername[20] = '\0';
    trim(ROM_PARAMS.headername); /* Remove trailing whitespace from ROM name. */

    rom_database_settings((const uint8_t*)&ROM_HEADER, &ROM_SETTINGS);

    /* print out a bunch of info about the ROM */
    core_log(CORE_LOG_INFO, "Name: %s", ROM_PARAMS.headername);
    imagestring(imagetype, buffer);
    core_log(CORE_LOG_INFO, "MD5: %s", ROM_SETTINGS.MD5);
    core_log(CORE_LOG_INFO, "CRC: %08" PRIX32 " %08" PRIX32, tohl(ROM_HEADER.CRC1), tohl(ROM_HEADER.CRC2));
    core_log(CORE_LOG_INFO, "Imagetype: %s", buffer);
    core_log(CORE_LOG_INFO, "Rom size: %d bytes (or %d Mb or %d Megabits)", g_rom_size, g_rom_size/1024/1024, g_rom_size/1024/1024*8);
    core_log(CORE_LOG_VERBOSE, "ClockRate = %" PRIX32, tohl(ROM_HEADER.ClockRate));
    core_log(CORE_LOG_INFO, "Version: %" PRIX32, tohl(ROM_HEADER.Release));
    if(tohl(ROM_HEADER.Manufacturer_ID) == 'N')
        core_log(CORE_LOG_INFO, "Manufacturer: Nintendo");
    else
        core_log(CORE_LOG_INFO, "Manufacturer: %" PRIX32, tohl(ROM_HEADER.Manufacturer_ID));
    core_log(CORE_LOG_VERBOSE, "Cartridge_ID: %" PRIX16, ROM_HEADER.Cartridge_ID);
    countrycodestring(ROM_HEADER.Country_code, buffer);
    core_log(CORE_LOG_INFO, "Country: %s", buffer);
    core_log(CORE_LOG_VERBOSE, "PC = %" PRIX32, tohl(ROM_HEADER.PC));
    core_log(CORE_LOG_VERBOSE, "Save type: %d", ROM_SETTINGS.savetype);

    return CORE_OK;
}

core_result close_rom(void)
{
    if (g_dev.r4300.jit) {
        uint64_t cache[2];
        r4300_jit_cache_stats(&g_dev.r4300, cache);
        core_log(CORE_LOG_INFO, "Cached CPU blocks: decoded=%" PRIu64 " restored=%" PRIu64,
            cache[0], cache[1]);
    }
#ifdef HAVE_N64EMUJIT
    if (g_dev.r4300.emumode == EMUMODE_DYNAREC) {
        uint64_t stats[4], optimized[2], code[3];
        r4300_jit_stats(&g_dev.r4300, stats);
        r4300_jit_optimization_stats(&g_dev.r4300, optimized);
        core_log(CORE_LOG_INFO, "n64emuJIT: blocks=%" PRIu64 " native=%" PRIu64
            " entries=%" PRIu64 " commits=%" PRIu64,
            stats[0], stats[1], stats[2], stats[3]);
        core_log(CORE_LOG_INFO, "n64emuJIT optimization: fast=%" PRIu64 " links=%" PRIu64 " direct=%" PRIu64,
            optimized[0], optimized[1], r4300_jit_direct_links(&g_dev.r4300));
        r4300_jit_code_stats(&g_dev.r4300, code);
        core_log(CORE_LOG_INFO, "n64emuJIT linkage: bytes=%" PRIu64 " relative_calls=%" PRIu64 " absolute_calls=%" PRIu64,
            code[0], code[1], code[2]);
    }
#endif
    r4300_jit_destroy(&g_dev.r4300);

    /* Clear Byte-swapped flag, since ROM is now deleted. */
    g_RomWordsLittleEndian = 0;
    core_log(CORE_LOG_STATUS, "Rom closed.");

    return CORE_OK;
}

/********************************************************************************************/
/* ROM utility functions */

// Get the system type associated to a ROM country code.
static core_system_type rom_country_code_to_system_type(uint16_t country_code)
{
    switch (country_code & UINT16_C(0xFF))
    {
        // PAL codes
        case 0x44:
        case 0x46:
        case 0x48:
        case 0x49:
        case 0x4c:
        case 0x50:
        case 0x53:
        case 0x55:
        case 0x57:
        case 0x58:
        case 0x59:
        case 0x5a:
            return SYSTEM_PAL;

        // NTSC codes
        case 0x37:
        case 0x41:
        case 0x45:
        case 0x4a:
        default: // Fallback for unknown codes
            return SYSTEM_NTSC;
    }
}
