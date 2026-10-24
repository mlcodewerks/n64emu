

#ifndef __ROM_H__
#define __ROM_H__

#include <stdint.h>

#include "core/types.h"

/* ROM Loading and Saving functions */

core_result open_rom(const unsigned char* romimage, unsigned int size);
core_result close_rom(void);

extern int g_rom_size;

typedef struct _rom_params
{
   core_system_type systemtype;
   char headername[21];  /* ROM Name as in the header, removing trailing whitespace */
} rom_params;

extern core_rom_header   ROM_HEADER;
extern rom_params        ROM_PARAMS;
extern core_rom_settings ROM_SETTINGS;

/* Supported rom compressiontypes. */
enum 
{
    UNCOMPRESSED,
    ZIP_COMPRESSION,
    GZIP_COMPRESSION,
    BZIP2_COMPRESSION,
    LZMA_COMPRESSION,
    SZIP_COMPRESSION
};

/* Supported rom image types. */
enum 
{
    Z64IMAGE,
    V64IMAGE,
    N64IMAGE
};

/* Supported CIC chips. */
enum
{
    CIC_NUS_6101,
    CIC_NUS_6102,
    CIC_NUS_6103,
    CIC_NUS_6105,
    CIC_NUS_6106
};

#endif /* __ROM_H__ */
