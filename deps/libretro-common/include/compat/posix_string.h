/*
 *  Copyright  (C) 2010-2020 The RetroArch team
 *
 * SPDX-License-Identifier: BSD-3-Clause
 * See ../../../../LICENSE for the license terms.
 */

#ifndef __LIBRETRO_SDK_COMPAT_POSIX_STRING_H
#define __LIBRETRO_SDK_COMPAT_POSIX_STRING_H

#include <retro_common_api.h>

#ifdef _MSC_VER
#include <compat/msvc.h>
#endif

RETRO_BEGIN_DECLS

#ifdef _WIN32
#undef strtok_r
#define strtok_r(str, delim, saveptr) retro_strtok_r__(str, delim, saveptr)

char *strtok_r(char *str, const char *delim, char **saveptr);
#endif

#ifdef _MSC_VER
#undef strcasecmp
#undef strdup
#define strcasecmp(a, b) retro_strcasecmp__(a, b)
#define strdup(orig)     retro_strdup__(orig)
int strcasecmp(const char *a, const char *b);
char *strdup(const char *orig);

/* isblank is available since MSVC 2013 */
#if _MSC_VER < 1800
#undef isblank
#define isblank(c)       retro_isblank__(c)
int isblank(int c);
#endif

#endif

RETRO_END_DECLS

#endif
