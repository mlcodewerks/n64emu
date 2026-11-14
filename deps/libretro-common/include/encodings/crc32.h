/*
 *  Copyright  (C) 2010-2020 The RetroArch team
 *
 * SPDX-License-Identifier: BSD-3-Clause
 * See ../../../../LICENSE for the license terms.
 */

#ifndef _LIBRETRO_ENCODINGS_CRC32_H
#define _LIBRETRO_ENCODINGS_CRC32_H

#include <stdint.h>
#include <stddef.h>

#include <retro_common_api.h>

RETRO_BEGIN_DECLS

uint32_t encoding_crc32(uint32_t crc, const uint8_t *buf, size_t len);
uint32_t file_crc32(uint32_t crc, const char *path);

RETRO_END_DECLS

#endif
