/*
 *  Copyright  (C) 2010-2020 The RetroArch team
 *
 * SPDX-License-Identifier: BSD-3-Clause
 * See ../../../../LICENSE for the license terms.
 */

#ifndef __LIBRETRO_SDK_COMPAT_STRL_H
#define __LIBRETRO_SDK_COMPAT_STRL_H

#include <string.h>
#include <stddef.h>

#if defined(RARCH_INTERNAL) && defined(HAVE_CONFIG_H)
#include "../../../config.h"
#endif

#include <retro_common_api.h>

RETRO_BEGIN_DECLS

#ifdef __MACH__
#ifndef HAVE_STRL
#define HAVE_STRL
#endif
#endif

#ifndef HAVE_STRL
/* Avoid possible naming collisions during link since
 * we prefer to use the actual name. */
#define strlcpy(dst, src, size) strlcpy_retro__(dst, src, size)

#define strlcat(dst, src, size) strlcat_retro__(dst, src, size)

size_t strlcpy(char *dest, const char *source, size_t size);
size_t strlcat(char *dest, const char *source, size_t size);

#endif

char *strldup(const char *s, size_t n);

RETRO_END_DECLS

#endif
