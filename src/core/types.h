#ifndef PARALLAX_TYPES_H
#define PARALLAX_TYPES_H
#include <stdint.h>
#if defined(_WIN32)
#define CORE_EXPORT __declspec(dllexport)
#define CORE_CALL __cdecl
#else
#define CORE_EXPORT __attribute__((visibility("default")))
#define CORE_CALL
#endif
typedef enum { CORE_OK = 0, CORE_INVALID_INPUT, CORE_NO_MEMORY, CORE_INTERNAL_ERROR,
    CORE_INVALID_STATE, CORE_BACKEND_ERROR, CORE_SYSTEM_ERROR } core_result;
typedef enum { CORE_LOG_ERROR = 1, CORE_LOG_WARNING, CORE_LOG_INFO,
    CORE_LOG_STATUS, CORE_LOG_VERBOSE } core_log_level;


/* ----------------------------------------- */
/* Structures to hold ROM image information  */
/* ----------------------------------------- */

typedef enum
{
    SYSTEM_NTSC = 0,
    SYSTEM_PAL,
    SYSTEM_MPAL
} core_system_type;

typedef enum
{
    SAVETYPE_EEPROM_4K       = 0,
    SAVETYPE_EEPROM_16K      = 1,
    SAVETYPE_SRAM            = 2,
    SAVETYPE_FLASH_RAM       = 3,
    SAVETYPE_CONTROLLER_PAK  = 4,
    SAVETYPE_NONE            = 5,
} core_save_type;

typedef struct
{
   uint8_t  init_PI_BSB_DOM1_LAT_REG;  /* 0x00 */
   uint8_t  init_PI_BSB_DOM1_PGS_REG;  /* 0x01 */
   uint8_t  init_PI_BSB_DOM1_PWD_REG;  /* 0x02 */
   uint8_t  init_PI_BSB_DOM1_PGS_REG2; /* 0x03 */
   uint32_t ClockRate;                 /* 0x04 */
   uint32_t PC;                        /* 0x08 */
   uint32_t Release;                   /* 0x0C */
   uint32_t CRC1;                      /* 0x10 */
   uint32_t CRC2;                      /* 0x14 */
   uint32_t Unknown[2];                /* 0x18 */
   uint8_t  Name[20];                  /* 0x20 */
   uint32_t unknown;                   /* 0x34 */
   uint32_t Manufacturer_ID;           /* 0x38 */
   uint16_t Cartridge_ID;              /* 0x3C - Game serial number  */
   uint16_t Country_code;              /* 0x3E */
} core_rom_header;

typedef struct
{
   char MD5[33];
   unsigned char savetype;
   unsigned char rumble;  /* 0 - No, 1 - Yes boolean for rumble support. */
   unsigned char transferpak; /* 0 - No, 1 - Yes boolean for transfer pak support. */
   unsigned char mempak; /* 0 - No, 1 - Yes boolean for memory pak support. */
   unsigned char biopak; /* 0 - No, 1 - Yes boolean for bio pak support. */
} core_rom_settings;

enum core_accessory { ACCESSORY_NONE = 1, ACCESSORY_MEMPAK = 2,
    ACCESSORY_RUMBLE_PAK = 3, ACCESSORY_TRANSFER_PAK = 4,
    ACCESSORY_RAW = 5, ACCESSORY_BIO_PAK = 6 };
#endif
