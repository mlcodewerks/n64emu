/*
 *  Copyright  (C) 2010-2020 The RetroArch team
 *
 * SPDX-License-Identifier: BSD-3-Clause
 * See ../../../../LICENSE for the license terms.
 */

#ifndef _LIBRETRO_ENCODINGS_UTF_H
#define _LIBRETRO_ENCODINGS_UTF_H

#include <stdint.h>
#include <stddef.h>

#include <boolean.h>

#include <retro_common_api.h>

RETRO_BEGIN_DECLS

enum CodePage
{
   CODEPAGE_LOCAL = 0, /* CP_ACP */
   CODEPAGE_UTF8  = 65001 /* CP_UTF8 */
};

size_t utf8_conv_utf32(uint32_t *out, size_t out_chars,
      const char *in, size_t in_size);

bool utf16_conv_utf8(uint8_t *out, size_t *out_chars,
      const uint16_t *in, size_t in_size);

size_t utf8len(const char *string);

size_t utf8cpy(char *d, size_t d_len, const char *s, size_t chars);

const char *utf8skip(const char *str, size_t chars);

uint32_t utf8_walk(const char **string);

bool utf16_to_char_string(const uint16_t *in, char *s, size_t len);

char* utf8_to_local_string_alloc(const char *str);

char* local_to_utf8_string_alloc(const char *str);

wchar_t* utf8_to_utf16_string_alloc(const char *str);

char* utf16_to_utf8_string_alloc(const wchar_t *str);

RETRO_END_DECLS

#endif
