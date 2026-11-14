/*
 *  Copyright  (C) 2010-2020 The RetroArch team
 *
 * SPDX-License-Identifier: BSD-3-Clause
 * See ../../../../LICENSE for the license terms.
 */

#ifndef __LIBRETRO_SDK_COMPAT_STRCASESTR_H
#define __LIBRETRO_SDK_COMPAT_STRCASESTR_H

#include <string.h>

#if defined(RARCH_INTERNAL) && defined(HAVE_CONFIG_H)
#include "../../../config.h"
#endif

#ifndef HAVE_STRCASESTR

#include <retro_common_api.h>

RETRO_BEGIN_DECLS

/* Avoid possible naming collisions during link
 * since we prefer to use the actual name. */
#define strcasestr(haystack, needle) strcasestr_retro__(haystack, needle)

char *strcasestr(const char *haystack, const char *needle);

RETRO_END_DECLS

#endif

#endif
